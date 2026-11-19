#include "game/bank34_misc.h"
#include "game/bank35_field.h"
#include "game/bank35_world.h"
#include "game/bank36_battle.h"
#include "game/bank37.h"
#include "game/bank3c_title.h"
#include "game/bank3d_sound.h"
#include "game/battle_actor_state.h"
#include "game/computed_flags.h"
#include "game/computed_handlers.h"
#include "game/hooks.h"
#include "game/session.h"
#include "game/system.h"
#include "game/tables.h"
#include "game/text.h"
#include "game/text_render.h"
// Battle damage/status rules, transformation menus, UI, and effects from bank 0x36 (segment 6000h).
#include "game/bank34.h"
#include "game/bank36_misc.h"
#include <array>
#include <initializer_list>
namespace sawgod::game {

// Canonical complete-announcement implementations shared with the original helper entries.
void show_target_description_message_core(Machine &m);
void show_actor_recovered_message_core(Machine &m);
void show_status_bit1_message_core(Machine &m);
void show_status_bit3_message_core(Machine &m);
void show_actor_amount_message_core(Machine &m);
void show_ability_replaced_message_core(Machine &m);

namespace {
constexpr uint16_t kLineCompareInterruptVector = 0x0090;
constexpr uint16_t kHblankTimerInterruptVector = 0x009C;
constexpr uint16_t kScreenMap = 0x1000;
constexpr uint16_t kScreenMap2 = 0x1800;
constexpr uint16_t kSpriteBuffer = 0xC000;
constexpr uint16_t kSpritePositions = 0xC002;
[[maybe_unused]] constexpr uint16_t kSpriteSixTileAttribute = 0xC019;
constexpr uint16_t kPaletteBuffer = 0xC400;
constexpr uint16_t kPaletteTargetBuffer = 0xC600;
constexpr uint16_t kKeysPressed = 0xC804;
constexpr uint16_t kKeysRepeat = 0xC806;
[[maybe_unused]] constexpr uint16_t kKeyRepeatTimer = 0xC808;
[[maybe_unused]] constexpr uint16_t kRandomSeed = 0xC80C;
[[maybe_unused]] constexpr uint16_t kOamCount = 0xC80F;
[[maybe_unused]] constexpr uint16_t kPlayerFacing = 0xC810;
[[maybe_unused]] constexpr uint16_t kBlinkEnabled = 0xC811;
[[maybe_unused]] constexpr uint16_t kFieldOverlayMode = 0xC812;
[[maybe_unused]] constexpr uint16_t kFieldWindowTopLeft = 0xC814;
[[maybe_unused]] constexpr uint16_t kFieldWindowBottomRight = 0xC816;
constexpr uint16_t kSavedSceneSp = 0xC818;
constexpr uint16_t kSceneIndex = 0xC81A;
[[maybe_unused]] constexpr uint16_t kIntroStrip = 0xC81C;
[[maybe_unused]] constexpr uint16_t kBattleSceneScratch = 0xC81E;
[[maybe_unused]] constexpr uint16_t kMusicRequest = 0xC820;
[[maybe_unused]] constexpr uint16_t kSfxRequest = 0xC821;
[[maybe_unused]] constexpr uint16_t kMusicCurrent = 0xC822;
[[maybe_unused]] constexpr uint16_t kFieldEventPending = 0xC825;
[[maybe_unused]] constexpr uint16_t kFieldEventKind = 0xC826;
[[maybe_unused]] constexpr uint16_t kFieldEventArgument = 0xC827;
[[maybe_unused]] constexpr uint16_t kInteractionState = 0xC82A;
[[maybe_unused]] constexpr uint16_t kDialogueSkipDelay = 0xC82B;
constexpr uint16_t kDebugForceMeatDrop = 0xC883;
constexpr uint16_t kBattleDebug = 0xC884;
constexpr uint16_t kNpcBackup =
    0xC885; // NPC backup; battle transitions reuse it as eighty four-byte raster rows
constexpr uint16_t kGameState = 0xD000;
[[maybe_unused]] constexpr uint16_t kMemberEquipmentFlags = 0xD00A;
[[maybe_unused]] constexpr uint16_t kSecondMemberEquipmentFlags = 0xD029;
[[maybe_unused]] constexpr uint16_t kThirdMemberEquipmentFlags = 0xD048;
[[maybe_unused]] constexpr uint16_t kFourthMemberEquipmentFlags = 0xD067;
[[maybe_unused]] constexpr uint16_t kInventory = 0xD07C;
[[maybe_unused]] constexpr uint16_t kInventorySlot0Uses = 0xD07D;
[[maybe_unused]] constexpr uint16_t kInventorySlot1Id = 0xD07E;
[[maybe_unused]] constexpr uint16_t kInventorySlot1Uses = 0xD07F;
[[maybe_unused]] constexpr uint16_t kInventorySlot2Id = 0xD080;
[[maybe_unused]] constexpr uint16_t kInventorySlot2Uses = 0xD081;
[[maybe_unused]] constexpr uint16_t kInventorySlot3Id = 0xD082;
[[maybe_unused]] constexpr uint16_t kInventorySlot3Uses = 0xD083;
[[maybe_unused]] constexpr uint16_t kInventorySlot4Id = 0xD084;
[[maybe_unused]] constexpr uint16_t kInventorySlot4Uses = 0xD085;
[[maybe_unused]] constexpr uint16_t kInventorySlot5Id = 0xD086;
[[maybe_unused]] constexpr uint16_t kInventorySlot5Uses = 0xD087;
[[maybe_unused]] constexpr uint16_t kInventorySlot6Id = 0xD088;
[[maybe_unused]] constexpr uint16_t kInventorySlot6Uses = 0xD089;
[[maybe_unused]] constexpr uint16_t kInventorySlot7Id = 0xD08A;
[[maybe_unused]] constexpr uint16_t kInventorySlot7Uses = 0xD08B;
[[maybe_unused]] constexpr uint16_t kGold = 0xD0A2;
[[maybe_unused]] constexpr uint16_t kGoldHigh = 0xD0A4;
[[maybe_unused]] constexpr uint16_t kSecondVehicleMap = 0xD0B3;
[[maybe_unused]] constexpr uint16_t kSecondVehicleX = 0xD0B4;
[[maybe_unused]] constexpr uint16_t kSecondVehicleY = 0xD0B5;
[[maybe_unused]] constexpr uint16_t kSecondVehicleState = 0xD0B6;
[[maybe_unused]] constexpr uint16_t kTravelMode = 0xD0D6;
[[maybe_unused]] constexpr uint16_t kPreviousMap = 0xD0D7;
[[maybe_unused]] constexpr uint16_t kPreviousPlayerX = 0xD0D8;
[[maybe_unused]] constexpr uint16_t kPreviousPlayerY = 0xD0D9;
[[maybe_unused]] constexpr uint16_t kCurrentMap = 0xD0DC;
[[maybe_unused]] constexpr uint16_t kPlayerX = 0xD0DD;
[[maybe_unused]] constexpr uint16_t kPlayerY = 0xD0DE;
constexpr uint16_t kDialogueLeader = 0xD0E3;
[[maybe_unused]] constexpr uint16_t kEndingSeen = 0xD0E5;
[[maybe_unused]] constexpr uint16_t kDialogueSpeed = 0xD0E6;
constexpr uint16_t kBattleOptions = 0xD0E7;
constexpr uint16_t kBattleTypePointers = 0xD1FB; // bank-36 ROM monster-record pointer table
constexpr uint16_t kFormationBackTileSource = 0xD500;
[[maybe_unused]] constexpr uint16_t kFieldPreSpriteCallback = 0xD501;
constexpr uint16_t kFormationBackPaletteSource = 0xD502;
[[maybe_unused]] constexpr uint16_t kFieldPostSpriteCallback = 0xD503;
[[maybe_unused]] constexpr uint16_t kFormationBackMapSource = 0xD504;
[[maybe_unused]] constexpr uint16_t kFieldNpcCallback = 0xD505;
[[maybe_unused]] constexpr uint16_t kFormationBackTileCount = 0xD506;
constexpr uint16_t kFormationBackPaletteVariant = 0xD508;
[[maybe_unused]] constexpr uint16_t kFormationBackWidth = 0xD509;
[[maybe_unused]] constexpr uint16_t kFormationBackHeight = 0xD50A;
constexpr uint16_t kFormationBackPaletteCount = 0xD50B;
constexpr uint16_t kFormationMiddleTileSource = 0xD50C;
[[maybe_unused]] constexpr uint16_t kBattleWipeThirdScroll = 0xD50D;
constexpr uint16_t kFormationMiddlePaletteSource = 0xD50E;
[[maybe_unused]] constexpr uint16_t kBattleWipeFourthScroll = 0xD50F;
[[maybe_unused]] constexpr uint16_t kFormationMiddleMapSource = 0xD510;
[[maybe_unused]] constexpr uint16_t kFieldScroll = 0xD511;
[[maybe_unused]] constexpr uint16_t kFormationMiddleTileCount = 0xD512;
[[maybe_unused]] constexpr uint16_t kIntroOverlayProgress = 0xD513;
constexpr uint16_t kFormationMiddlePaletteVariant = 0xD514;
[[maybe_unused]] constexpr uint16_t kFormationMiddleWidth = 0xD515;
[[maybe_unused]] constexpr uint16_t kFormationMiddleHeight = 0xD516;
constexpr uint16_t kFormationMiddlePaletteCount = 0xD517;
[[maybe_unused]] constexpr uint16_t kFormationFrontTileSource = 0xD518;
[[maybe_unused]] constexpr uint16_t kFieldFadeActive = 0xD519;
constexpr uint16_t kFormationFrontPaletteSource = 0xD51A;
[[maybe_unused]] constexpr uint16_t kFieldPaletteTimer = 0xD51B;
[[maybe_unused]] constexpr uint16_t kFormationFrontMapSource = 0xD51C;
[[maybe_unused]] constexpr uint16_t kFormationFrontTileCount = 0xD51E;
[[maybe_unused]] constexpr uint16_t kFieldTileAnimationTimer = 0xD51F;
constexpr uint16_t kFormationFrontPaletteVariant = 0xD520;
[[maybe_unused]] constexpr uint16_t kFormationFrontWidth = 0xD521;
[[maybe_unused]] constexpr uint16_t kFormationFrontHeight = 0xD522;
constexpr uint16_t kFormationFrontPaletteCount = 0xD523;
constexpr uint16_t kFormationFrontPosition = 0xD524;
constexpr uint16_t kFormationEffectTileSource = 0xD526;
constexpr uint16_t kFormationEffectPaletteSource = 0xD528;
[[maybe_unused]] constexpr uint16_t kFormationEffectMapSource = 0xD52A;
[[maybe_unused]] constexpr uint16_t kOverlayEnabled = 0xD52B;
constexpr uint16_t kFormationEffectTileCount = 0xD52C;
constexpr uint16_t kFormationEffectPaletteVariant = 0xD52E;
constexpr uint16_t kFormationEffectWidth = 0xD52F;
constexpr uint16_t kFormationEffectHeight = 0xD530;
constexpr uint16_t kFormationEffectPaletteCount = 0xD531;
constexpr uint16_t kMemberPaletteOffsets = 0xD535;
[[maybe_unused]] constexpr uint16_t kFieldAnimationPaletteSource = 0xD538;
constexpr uint16_t kMemberPaletteSegments = 0xD53D;
constexpr uint16_t kMemberPaletteSlots = 0xD545;
constexpr uint16_t kMemberPaletteVariants = 0xD54D;
constexpr uint16_t kPortraitPaletteSource = 0xD553;
constexpr uint16_t kPortraitPaletteBank = 0xD559;
constexpr uint16_t kIconQueue = 0xD55A;
[[maybe_unused]] constexpr uint16_t kCamX = 0xD59D;
[[maybe_unused]] constexpr uint16_t kCamY = 0xD59F;
[[maybe_unused]] constexpr uint16_t kNpcLastMatch = 0xD5A1;
constexpr uint16_t kPopupState = 0xD60E;
constexpr uint16_t kBattleActorIcons = 0xD60F;
constexpr uint16_t kBattleTargetIcons = 0xD62F;
constexpr uint16_t kBattleItemIcons = 0xD64F;
constexpr uint16_t kBattleNumberText = 0xD66F;
constexpr uint16_t kRasterWaveOffset = 0xD6B5;
constexpr uint16_t kRasterWavePhase = 0xD6B6;
constexpr uint16_t kRasterWaveSource = 0xD6B7;
constexpr uint16_t kEnemyCount = 0xD6D1;
[[maybe_unused]] constexpr uint16_t kEnemyGroupCount = 0xD6D4;
constexpr uint16_t kBattleMember = 0xD6E1;
constexpr uint16_t kBattleMenuState = 0xD6E2;
constexpr uint16_t kBattleMenuHistory = 0xD6E3;
[[maybe_unused]] constexpr uint16_t kEncounterVariant = 0xD6EE;
[[maybe_unused]] constexpr uint16_t kLineScrollTable = 0xD6FE;
[[maybe_unused]] constexpr uint16_t kIntroProjectileTicks = 0xD6FF;
[[maybe_unused]] constexpr uint16_t kPartyMenuEntryContext = 0xD700;
[[maybe_unused]] constexpr uint16_t kPartyMenuText = 0xD701;
[[maybe_unused]] constexpr uint16_t kIntroProjectileState = 0xD702;
[[maybe_unused]] constexpr uint16_t kIntroProjectileXOffset = 0xD703;
[[maybe_unused]] constexpr uint16_t kIntroProjectilePhase = 0xD705;
[[maybe_unused]] constexpr uint16_t kIntroProjectileFrame = 0xD706;
[[maybe_unused]] constexpr uint16_t kIntroProjectileYOffset = 0xD707;
[[maybe_unused]] constexpr uint16_t kIntroProjectilePosition = 0xD708;
[[maybe_unused]] constexpr uint16_t kIntroProjectileOrigin = 0xD70A;
constexpr uint16_t kBattleMenuHistoryCount = 0xD70B;
[[maybe_unused]] constexpr uint16_t kIntroProjectileStep = 0xD70C;
constexpr uint16_t kFormationKind = 0xD70D;
constexpr uint16_t kBattleFlags = 0xD70E;
constexpr uint16_t kFormationPalettePhase = 0xD70F;
constexpr uint16_t kTransformationStage = 0xD712;
constexpr uint16_t kTransformationMember = 0xD713;
constexpr uint16_t kTransformationChoice = 0xD714;
constexpr uint16_t kNewActorType = 0xD715;
constexpr uint16_t kOldActorType = 0xD716;
constexpr uint16_t kFormationGraphicsIndex = 0xD717;
constexpr uint16_t kMeatSourceType = 0xD719;
constexpr uint16_t kBattleActors = 0xD71A;
[[maybe_unused]] constexpr uint16_t kTabActionIndex = 0xD840;
[[maybe_unused]] constexpr uint16_t kRaceSelectIndex = 0xD841;
[[maybe_unused]] constexpr uint16_t kTabClassSelectIndex = 0xD842;
[[maybe_unused]] constexpr uint16_t kTabGenderSelectIndex = 0xD843;
[[maybe_unused]] constexpr uint16_t kTabThirdSelection = 0xD844;
[[maybe_unused]] constexpr uint16_t kTabFourthSelection = 0xD845;
[[maybe_unused]] constexpr uint16_t kTabFifthSelection = 0xD846;
[[maybe_unused]] constexpr uint16_t kTabSixthSelection = 0xD847;
[[maybe_unused]] constexpr uint16_t kTabExitRequested = 0xD84A;
[[maybe_unused]] constexpr uint16_t kTabActionResult = 0xD84B;
[[maybe_unused]] constexpr uint16_t kMenuPriceLow = 0xD84C;
[[maybe_unused]] constexpr uint16_t kMenuPriceHigh = 0xD84E;
[[maybe_unused]] constexpr uint16_t kMenuDescriptionSpriteStart = 0xD850;
[[maybe_unused]] constexpr uint16_t kTabListColumn = 0xD852;
[[maybe_unused]] constexpr uint16_t kTabEquipmentMember = 0xD853;
[[maybe_unused]] constexpr uint16_t kTabSourceColumn = 0xD854;
[[maybe_unused]] constexpr uint16_t kTabItemBuffer = 0xD855;
[[maybe_unused]] constexpr uint16_t kNameText = 0xD856;
[[maybe_unused]] constexpr uint16_t kTabSecondItemBuffer = 0xD857;
[[maybe_unused]] constexpr uint16_t kTabTransferState = 0xD859;
[[maybe_unused]] constexpr uint16_t kShopCursorBlink = 0xD85D;
constexpr uint16_t kBattleOrder = 0xDB76;
constexpr uint16_t kBattleStatusSuccessCount = 0xDBF3;
constexpr uint16_t kItemUseCount = 0xDBF4;
constexpr uint16_t kBattleKnockoutMode = 0xDBF6;
constexpr uint16_t kBattleFlashCounter = 0xDBF7;
constexpr uint16_t kBattleGroupEliminationScratch = 0xDBF9;
constexpr uint16_t kBattleEffectReflected = 0xDBFA;
constexpr uint16_t kBattleReflectionActor = 0xDBFB;
constexpr uint16_t kActionId = 0xDBFC;
constexpr uint16_t kActionEffect = 0xDBFD;
constexpr uint16_t kActingActor = 0xDBFE;
constexpr uint16_t kActionTarget = 0xDBFF;
constexpr uint16_t kTargetGroup = 0xDC00;
constexpr uint16_t kAiDisposition = 0xDC03;
constexpr uint16_t kBattleSavedAttackValue = 0xDC09;
constexpr uint16_t kBattleItemPower = 0xDC0A;
constexpr uint16_t kMeterInputTarget = 0xDC0B; // signed pending damage; negative values heal
constexpr uint16_t kBattleMultiHitCount = 0xDC11;
constexpr uint16_t kEffectPaletteVariant = 0xDC12;
constexpr uint16_t kBlinkPhase = 0xDC17;
[[maybe_unused]] constexpr uint16_t kEffectTicks = 0xDC18;
[[maybe_unused]] constexpr uint16_t kEffectFrame = 0xDC19;
constexpr uint16_t kMemberTargets = 0xDC20;
constexpr uint16_t kEquipmentRow = 0xDC62;
constexpr uint16_t kTargetSlot = 0xDC66;
constexpr uint16_t kMemberActionFlags = 0xDC6A;
constexpr uint16_t kEquipmentPage = 0xDC6E;
constexpr uint16_t kTextCursorPos = 0xDC75;
constexpr uint16_t kDigits = 0xDC7B;
constexpr uint16_t kEffectX = 0xDCBB;
constexpr uint16_t kEffectY = 0xDCBC;
[[maybe_unused]] constexpr uint16_t kEffectRowY = 0xDCBD;
[[maybe_unused]] constexpr uint16_t kEffectHeight = 0xDCBE;
[[maybe_unused]] constexpr uint16_t kEffectSpriteOffset = 0xDCBF;
constexpr uint16_t kGroupDrainSavedTarget = 0xDCC1;
constexpr uint16_t kTextColorScratch = 0xDCD3;
constexpr uint16_t kBattleImmunityMode = 0xDCD4;
constexpr uint16_t kActorGraphicsTile = 0xDCD5;
constexpr uint16_t kActorGraphicsPalette = 0xDCD7;
constexpr uint16_t kTransformationNewStats = 0xDCD9;
constexpr uint16_t kTransformationNewAbilityCount = 0xDCDD;
constexpr uint16_t kTransformationNewHp = 0xDCDE;
constexpr uint16_t kTransformationNewAbilities = 0xDCE0;
constexpr uint16_t kTransformationOldStats = 0xDCE8;
[[maybe_unused]] constexpr uint16_t kEffectMotionCounter = 0xDCEC;
[[maybe_unused]] constexpr uint16_t kEffectTravelOffset = 0xDCFC;
[[maybe_unused]] constexpr uint16_t kDialogueCursorPosition = 0xDD28;
[[maybe_unused]] constexpr uint16_t kFlarePhaseTicks = 0xDD29;
constexpr uint16_t kDebugMenuRow = 0xDD2A;
constexpr uint16_t kDebugListRow = 0xDD2B;
constexpr uint16_t kDebugMapId = 0xDD2D;
constexpr uint16_t kDebugBattleId = 0xDD2E;
constexpr uint16_t kDebugAutoWin = 0xDD2F;
constexpr uint16_t kDebugSfxEnabled = 0xDD30;
constexpr uint16_t kDebugPartySlot = 0xDD31;
constexpr uint16_t kDebugStatusRecords = 0xDD32;
constexpr uint16_t kBattleStrengthBonuses = 0xDD5A;
constexpr uint16_t kMemberSkipFlags = 0xDD66;
constexpr uint16_t kEffectTargetMode = 0xDD73;
constexpr uint16_t kCursorHidden = 0xDD75;
constexpr uint16_t kSpecialActorPreviousHp = 0xEC7A;
constexpr uint16_t kSpecialActorUnchangedHpCount = 0xEC7C;
constexpr uint16_t kBattleMode = 0xEC7D;
constexpr uint16_t kPanelSpecialMode = 0xEC81;
constexpr uint16_t kTransformationUnchanged = 0xEC82;
constexpr uint16_t kFooterPanelMode = 0xEC83;
constexpr uint16_t kGroupDrainTargetCount = 0xEC84;
constexpr uint16_t kGroupDrainKnockoutCount = 0xEC85;
constexpr uint16_t kGroupDrainPreviousGroup = 0xEC86;
constexpr uint16_t kVignetteBackdropColor = 0xFFE0;
void load_battle_palette_core(Machine &m, uint16_t palette_destination) {
    const auto ax = m.ax(), cx = m.cx(), si = m.si(), di = m.di(), ds = m.ds();
    m.ds() = 0x2000;
    m.out8(0xC2, m.rd8(m.ss(), kPortraitPaletteBank));
    m.cx() = 4;
    m.si() = m.rd16(m.ss(), kPortraitPaletteSource);
    m.ax() = 0;
    m.di() = palette_destination;
    copy_palettes_core(m);
    m.ax() = ax;
    m.cx() = cx;
    m.si() = si;
    m.di() = di;
    m.ds() = ds;
}
void clear_battle_region_core(Machine &m, uint8_t first_column, uint8_t region_width) {
    const auto ax = m.ax(), bx = m.bx(), cx = m.cx(), dx = m.dx();
    for (uint16_t y = 0; y < 12; ++y)
        for (uint16_t x = 0; x < region_width; ++x) {
            m.ds() = 0x9000;
            m.si() = 0x227E;
            // Left pane resets BL from AL: first row draws column 1 twice; later rows start at 0.
            const uint16_t column =
                first_column == 1 && region_width == 11 ? (x == 0 && y == 0 ? 1 : x) : first_column + x;
            m.bx() = uint16_t((y << 8) | column);
            m.cx() = 0x0101;
            m.dx() = 0x1F00;
            m.ax() = 1;
            draw_tilemap_offset_core(m);
        }
    m.ax() = ax;
    m.bx() = bx;
    m.cx() = cx;
    m.dx() = dx;
}
void position_debug_cursor_core(Machine &m, uint16_t row_address, bool double_row_spacing, uint8_t cursor_x) {
    uint8_t tile_row = m.rd8(m.ss(), row_address);
    if (double_row_spacing)
        tile_row = uint8_t(tile_row * 2);
    tile_row = uint8_t(tile_row + 4);
    m.dx() = uint16_t(tile_row << 8);
    m.ax() = uint16_t((cursor_x << 8) | uint8_t(uint16_t(tile_row) * 8 - 4));
    m.bx() = m.ax();
    m.wr16(m.ss(), kSpritePositions, m.bx());
    m.bx() = uint16_t((uint8_t(cursor_x + 8) << 8) | m.al());
    m.wr16(m.ss(), kSpritePositions + 4, m.bx());
    m.bx() = uint16_t((cursor_x << 8) | uint8_t(m.al() + 8));
    m.wr16(m.ss(), kSpritePositions + 8, m.bx());
    m.bx() = uint16_t((uint8_t(cursor_x + 8) << 8) | uint8_t(m.al() + 8));
    m.wr16(m.ss(), kSpritePositions + 12, m.bx());
}
void draw_debug_panel_core(Machine &m) {
    m.ax() = 1;
    m.bx() = 0;
    m.cx() = 0x2020;
    m.dx() = 0x1FF;
    fill_screen_rect_core(m);
    m.ax() = 0;
    m.bx() = 1;
    m.cx() = 0x021C;
    draw_window_core(m);
    m.ax() = 0;
    m.bx() = 0x201;
    m.cx() = 0x101C;
    draw_window_core(m);
}
void draw_debug_label_core(Machine &m, TextKey key, uint16_t position) {
    const TextPtr label_text = text(m, key);
    m.ds() = label_text.segment;
    m.si() = label_text.offset;
    m.bx() = position;
    m.ax() = 1;
    draw_text_core(m);
    text_done(m, key);
}
} // namespace
// Shared body of ROM 0x364A84
// Copies four palettes from the mapped SRAM source to C400h; preserves registers and selects ROM bank C2h.
void load_battle_palette_backup_core(Machine &m) {
    load_battle_palette_core(m, kPaletteBuffer);
    return;
}

// rom: 0x364A84
void load_battle_palette_backup(Machine &m) {
    load_battle_palette_backup_core(m);
    m.retf();
}
// rom: 0x364AAC
// Copies four palettes from the mapped SRAM source to C600h; preserves registers and selects ROM bank C2h.
void load_battle_palette_display_core(Machine &m) {
    load_battle_palette_core(m, kPaletteTargetBuffer);
    return;
}

void load_battle_palette_display(Machine &m) {
    load_battle_palette_display_core(m);
    m.retf();
}
// Shared body of ROM 0x36517E
void build_name_string_core(Machine &m) {
    const int name_run_id =
        name_run(m, NameTable::Monster, m.al()); // a loaded language's name (text.h): one word
    if (name_run_id >= 0) {
        m.wr16(m.ss(), m.di(), uint16_t(kTextWord | name_run_id));
        m.wr16(m.ss(), uint16_t(m.di() + 2), 0xFFFF);
        return;
    }
    const TextPtr name_record_ptr = name_record(m, NameTable::Monster, m.al());
    uint16_t si = name_record_ptr.offset, di = m.di();
    for (;;) {
        uint8_t glyph_code = m.rd8(name_record_ptr.segment, si);
        if (glyph_code == 0xD9 || glyph_code == 0xFF)
            break;
        m.wr16(m.ss(), di, uint16_t(glyph_code * 2));
        si = uint16_t(si + 1);
        di = uint16_t(di + 2);
    }
    m.wr16(m.ss(), di, 0xFFFF);
    return;
}

// rom: 0x36517E
// Expands AL's nine-byte name record into doubled tile words at SS:DI, terminated by FFFFh. Registers
// preserved.
void build_name_string(Machine &m) {
    build_name_string_core(m);
    m.retf();
}
// rom: 0x36528E
// Copies four alternate marker tile words from bank 38 to the sprite table, using BX/DX offsets. SI/DI
// advance.
void draw_battle_marker_variant(Machine &m) {
    m.si() = uint16_t(0xF00 + m.dx() + (m.rd8(0, kBlinkPhase) ? 8 : 0));
    m.di() = uint16_t(kSpriteBuffer + m.bx());
    m.dx() = 0xE40;
    for (unsigned sprite_index = 0; sprite_index < 4; ++sprite_index) {
        m.wr16(0, m.di(), uint16_t(m.rd16(0x8000, m.si()) + m.dx()) | 0x2000);
        m.di() = uint16_t(m.di() + 4);
        m.si() = uint16_t(m.si() + 2);
    }
    m.retf();
}
// Shared body of ROM 0x36548C
void clear_battle_background_core(Machine &m) {
    clear_battle_region_core(m, 0, 30);
    return;
}

// rom: 0x36548C
// Clears the 30x12 battle background with the blank tile; AX/BX/CX/DX preserved, DS/SI left on the blank
// tile.
void clear_battle_background(Machine &m) {
    clear_battle_background_core(m);
    m.retf();
}
// Shared body of ROM 0x3654D1
void clear_battle_left_pane_core(Machine &m) {
    clear_battle_region_core(m, 1, 11);
    return;
}

// rom: 0x3654D1
// Clears the left 11x12 battle pane; preserves AX/BX/CX/DX.
void clear_battle_left_pane(Machine &m) {
    clear_battle_left_pane_core(m);
    m.retf();
}
// Shared body of ROM 0x365516
void clear_battle_right_pane_core(Machine &m) {
    clear_battle_region_core(m, 15, 11);
    return;
}

// rom: 0x365516
// Clears the right 11x12 battle pane; preserves AX/BX/CX/DX.
void clear_battle_right_pane(Machine &m) {
    clear_battle_right_pane_core(m);
    m.retf();
}
// Shared body of ROM 0x36555B
void clear_battle_inner_pane_core(Machine &m) {
    clear_battle_region_core(m, 1, 29);
    return;
}

// rom: 0x36555B
// Clears the inner 29x12 battle pane; preserves AX/BX/CX/DX.
void clear_battle_inner_pane(Machine &m) {
    clear_battle_inner_pane_core(m);
    m.retf();
}
// Shared body of ROM 0x365865
void draw_debug_extended_menu_core(Machine &m) {
    draw_debug_panel_core(m);
    constexpr std::array<TextKey, 13> label_keys = {
        TextKey::draw_debug_extended_menu_1,  TextKey::draw_debug_extended_menu_2,
        TextKey::draw_debug_extended_menu_3,  TextKey::draw_debug_extended_menu_4,
        TextKey::draw_debug_extended_menu_5,  TextKey::draw_debug_extended_menu_6,
        TextKey::draw_debug_extended_menu_7,  TextKey::draw_debug_extended_menu_8,
        TextKey::draw_debug_extended_menu_9,  TextKey::draw_debug_extended_menu_10,
        TextKey::draw_debug_extended_menu_11, TextKey::draw_debug_extended_menu_12,
        TextKey::draw_debug_extended_menu_13};
    constexpr std::array<uint16_t, 13> label_positions = {0x101, 0x402, 0x503, 0x603, 0x703, 0x803, 0x903,
                                                          0xA03, 0xB03, 0xC03, 0xD03, 0xE03, 0xF02};
    for (unsigned label_index = 0; label_index < label_keys.size(); ++label_index)
        draw_debug_label_core(m, label_keys[label_index], label_positions[label_index]);
    return;
}

// rom: 0x365865
// Draws the extended debug menu's windows and fixed text labels on screen page one.
void draw_debug_extended_menu(Machine &m) {
    draw_debug_extended_menu_core(m);
    m.retf();
}
// Shared body of ROM 0x36598A
void draw_debug_settings_menu_core(Machine &m) {
    draw_debug_panel_core(m);
    constexpr std::array<TextKey, 7> text = {
        TextKey::draw_debug_settings_menu_1, TextKey::draw_debug_settings_menu_2,
        TextKey::draw_debug_settings_menu_3, TextKey::draw_debug_settings_menu_4,
        TextKey::draw_debug_settings_menu_5, TextKey::draw_debug_settings_menu_6,
        TextKey::draw_debug_settings_menu_7};
    constexpr std::array<uint16_t, 7> label_positions = {0x101, 0x403, 0x603, 0x803, 0xA03, 0xC03, 0xE03};
    for (unsigned label_index = 0; label_index < label_positions.size(); ++label_index)
        draw_debug_label_core(m, text[label_index], label_positions[label_index]);
    return;
}

// rom: 0x36598A
// Draws the debug settings menu's windows and fixed text labels on screen page one.
void draw_debug_settings_menu(Machine &m) {
    draw_debug_settings_menu_core(m);
    m.retf();
}
// Shared body of ROM 0x365C25
void position_debug_settings_cursor_core(Machine &m) {
    position_debug_cursor_core(m, kDebugMenuRow, true, 8);
    return;
}

// rom: 0x365C25 (near)
// Positions the four-sprite debug cursor from DD2Ah, with two rows per selection. AX/BX/DX are leftovers.
void position_debug_settings_cursor(Machine &m) {
    position_debug_settings_cursor_core(m);
    m.ret();
}
// Shared body of ROM 0x365C64
void position_debug_list_cursor_core(Machine &m) {
    position_debug_cursor_core(m, kDebugListRow, false, 0);
    return;
}

// rom: 0x365C64 (near)
// Positions the four-sprite list cursor from DD2Bh. AX/BX/DX are leftovers.
void position_debug_list_cursor(Machine &m) {
    position_debug_list_cursor_core(m);
    m.ret();
}

namespace {
// Original PUSHA/segment saves preserve registers, while CLD remains observable.
struct SavedPortraitRegisters {
    uint16_t ax, bx, cx, dx, si, di, ds;
    explicit SavedPortraitRegisters(Machine &m)
        : ax(m.ax()), bx(m.bx()), cx(m.cx()), dx(m.dx()), si(m.si()), di(m.di()), ds(m.ds()) {}
    void restore(Machine &m) const {
        m.ax() = ax;
        m.bx() = bx;
        m.cx() = cx;
        m.dx() = dx;
        m.si() = si;
        m.di() = di;
        m.ds() = ds;
    }
};
void load_portrait_palette_core(Machine &m, uint16_t formation_record, uint16_t palette_slot) {
    m.ds() = 0xE000;
    m.si() = m.rd16(m.ss(), uint16_t(formation_record + 2));
    m.si() = uint16_t(m.si() + uint16_t(m.rd8(m.ss(), uint16_t(formation_record + 8))) * 32);
    m.ax() = palette_slot;
    m.cx() = m.rd8(m.ss(), uint16_t(formation_record + 11));
    m.di() = kPaletteBuffer;
    copy_palettes_core(m);
}
uint16_t portrait_position(uint16_t packed_position, uint8_t portrait_width) {
    uint8_t x = uint8_t(packed_position), y = uint8_t(packed_position >> 8);
    if (portrait_width == 6)
        x = uint8_t(x - 1);
    else if (portrait_width == 9) {
        x = uint8_t(x - 2);
        y = uint8_t(y - 2);
    }
    return uint16_t((y << 8) | x);
}
void load_formation_tint_core(Machine &m, uint16_t tint_source) {
    const uint16_t ds = m.ds();
    m.wr8(m.ss(), kFormationPalettePhase, 0);
    m.set_al(m.rd8(m.ss(), kFormationKind));
    const unsigned layer_index = m.al() == 1   ? 1
                                 : m.al() == 2 ? 2
                                               : 0; // values outside 0..2 fall through to slot zero
    m.bx() = uint16_t(4 + layer_index);
    m.cx() = m.rd8(m.ss(), uint16_t(kFormationBackTileSource + 12 * layer_index + 11));
    m.ds() = 0xE000;
    m.ax() = m.bx();
    m.si() = tint_source;
    m.di() = kPaletteBuffer;
    copy_palettes_core(m);
    m.ds() = ds;
}
} // namespace
// Verified with populated transformation-preview records; ordinary movie snapshots have no valid records.
// Shared body of ROM 0x36CAD6
// Loads tiles/palettes for two formation portraits, then draws them on page one.
void draw_formation_comparison_portraits_core(Machine &m) {
    const SavedPortraitRegisters saved(m);
    m.ds() = 0xE000;
    m.si() = m.rd16(m.ss(), kFormationBackTileSource + 12);
    m.cx() = m.rd16(m.ss(), kFormationBackTileSource + 18);
    m.ax() = 0x328;
    copy_tiles_core(m);
    load_portrait_palette_core(m, kFormationBackTileSource + 12, 5);
    m.bx() = portrait_position(0x103, uint8_t(m.cx()));
    m.si() = m.rd16(m.ss(), kFormationBackTileSource + 16);
    m.cx() = uint16_t(m.rd8(m.ss(), kFormationBackTileSource + 21) |
                      (m.rd8(m.ss(), kFormationBackTileSource + 22) << 8));
    m.ds() = 0xE000;
    m.ax() = 1;
    m.dx() = 0xB28;
    draw_tilemap_offset_core(m);
    m.si() = m.rd16(m.ss(), kFormationBackTileSource);
    m.cx() = m.rd16(m.ss(), kFormationBackTileSource + 6);
    m.ax() = 0x300;
    copy_tiles_core(m);
    load_portrait_palette_core(m, kFormationBackTileSource, 4);
    m.si() = m.rd16(m.ss(), kFormationBackTileSource + 4);
    m.cx() = uint16_t(m.rd8(m.ss(), kFormationBackTileSource + 9) |
                      (m.rd8(m.ss(), kFormationBackTileSource + 10) << 8));
    m.bx() = portrait_position(0x115, uint8_t(m.cx()));
    m.ds() = 0xE000;
    m.ax() = 1;
    m.dx() = 0x900;
    draw_tilemap_offset_core(m);
    saved.restore(m);
    return;
}

// rom: 0x36CAD6
// Loads tiles/palettes for two formation portraits, then draws them on page one.
void draw_formation_comparison_portraits(Machine &m) {
    draw_formation_comparison_portraits_core(m);
    m.retf();
}
// Shared body of ROM 0x36CC7D
// Restores AL-selected formation portrait's normal palette at palette 4/5/6. Invalid selections use slot
// zero. Registers preserved; source is the selected 12-byte record's palette pointer plus variant*32.
void restore_formation_portrait_palette_core(Machine &m) {
    const SavedPortraitRegisters saved(m);
    const unsigned layer_index = m.al() == 1 ? 1 : m.al() == 2 ? 2 : 0;
    load_portrait_palette_core(m, uint16_t(kFormationBackTileSource + 12 * layer_index),
                               uint16_t(4 + layer_index));
    saved.restore(m);
    return;
}

// rom: 0x36CC7D
void restore_formation_portrait_palette(Machine &m) {
    restore_formation_portrait_palette_core(m);
    m.retf();
}
// Shared body of ROM 0x36D10E
// Applies formation tint table BC20h to the selected portrait, clears the palette phase; DS preserved,
// AX=0, BX=palette, CX=count, SI/DI advance past the copied palettes.
void apply_formation_portrait_tint1_core(Machine &m) {
    load_formation_tint_core(m, 0xBC20);
    return;
}

// rom: 0x36D10E
void apply_formation_portrait_tint1(Machine &m) {
    apply_formation_portrait_tint1_core(m);
    m.retf();
}
// Shared body of ROM 0x36D15D
// Applies formation tint table BE80h to the selected portrait; same register effects as tint1.
void apply_formation_portrait_tint2_core(Machine &m) {
    load_formation_tint_core(m, 0xBE80);
    return;
}

// rom: 0x36D15D
void apply_formation_portrait_tint2(Machine &m) {
    apply_formation_portrait_tint2_core(m);
    m.retf();
}
// Shared body of ROM 0x36D1AC
// Applies formation tint table BD60h to the selected portrait; same register effects as tint1.
void apply_formation_portrait_tint3_core(Machine &m) {
    load_formation_tint_core(m, 0xBD60);
    return;
}

// rom: 0x36D1AC
void apply_formation_portrait_tint3(Machine &m) {
    apply_formation_portrait_tint3_core(m);
    m.retf();
}

namespace {
void battle_push(Machine &m, uint16_t stack_word) {
    m.sp() = uint16_t(m.sp() - 2);
    m.wr16(m.ss(), m.sp(), stack_word);
}
uint16_t battle_pop(Machine &m) {
    uint16_t stack_word = m.rd16(m.ss(), m.sp());
    m.sp() = uint16_t(m.sp() + 2);
    return stack_word;
}
void battle_save(Machine &m) {
    uint16_t sp = m.sp();
    for (uint16_t register_value :
         {m.ax(), m.cx(), m.dx(), m.bx(), sp, m.bp(), m.si(), m.di(), m.ds(), m.es(), m.ss()})
        battle_push(m, register_value);
}
void battle_restore(Machine &m) {
    m.ss() = battle_pop(m);
    m.es() = battle_pop(m);
    m.ds() = battle_pop(m);
    m.di() = battle_pop(m);
    m.si() = battle_pop(m);
    m.bp() = battle_pop(m);
    (void)battle_pop(m);
    m.bx() = battle_pop(m);
    m.dx() = battle_pop(m);
    m.cx() = battle_pop(m);
    m.ax() = battle_pop(m);
}
} // namespace
// rom: 0x361089 (near)
// Choose the fifth actor's special AI action from HP thresholds, forcing C9 after two unchanged-HP checks.
void choose_special_actor_action_core(Machine &m) {
    battle_save(m);
    m.di() = (kBattleOrder + 0x10);
    m.ax() = 0x10;
    m.set_al(0xFF);
    m.wr8(m.ss(), m.di(), m.al());
    m.wr8(m.ss(), uint16_t(m.di() + 3), 4);
    m.set_al(4);
    battle_actor_pointer_core(m);
    m.bx() = m.rd16(m.ss(), uint16_t(m.si() + 3));
    m.dx() = m.rd16(m.ss(), kSpecialActorPreviousHp);
    m.wr16(m.ss(), kSpecialActorPreviousHp, m.bx());
    bool force_fixed_action = false;
    if (m.bx() == m.dx()) {
        m.set_al(uint8_t(m.rd8(m.ss(), kSpecialActorUnchangedHpCount) + 1));
        m.wr8(m.ss(), kSpecialActorUnchangedHpCount, m.al());
        if (m.al() >= 2) {
            m.set_al(0);
            m.wr8(m.ss(), kSpecialActorUnchangedHpCount, 0);
            m.set_al(0xC9);
            force_fixed_action = true;
        }
    }
    // Select the first descending HP threshold met; two consecutive equal-HP checks force action C9h.
    if (!force_fixed_action) {
        m.si() = 0x19DC;
        m.ax() = 0x8000;
        m.ds() = m.ax();
        m.cx() &= 0xFF00;
        for (;;) {
            m.dx() = m.rd16(m.ds(), m.si());
            if (m.bx() >= m.dx())
                break;
            m.si() = uint16_t(m.si() + 2);
            m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(uint8_t(m.cx()) + 1));
        }
        m.ax() = uint8_t(m.cx());
        m.si() = uint16_t(0x19D4 + m.ax());
        m.set_al(m.rd8(m.ds(), m.si()));
    }
    m.wr8(m.ss(), m.di(), m.al());
    m.wr8(m.ss(), kAiDisposition, m.al());
    m.wr8(m.ss(), uint16_t(m.di() + 3), 4);
    m.set_al(4);
    battle_actor_pointer_core(m);
    m.dx() = m.si();
    resolve_ai_action_fallback_core(m);
    battle_restore(m);
    return;
}

void choose_special_actor_action(Machine &m) {
    choose_special_actor_action_core(m);
    m.ret();
}
// Shared body of ROM 0x3640A7
// Swap attacking/target actor and action IDs, and rebuild the current name-record index.
void swap_battle_action_sides_core(Machine &m) {
    uint16_t si = m.si(), cx = m.cx();
    m.cx() = uint16_t(m.rd8(m.ss(), kActingActor) | (m.rd8(m.ss(), kActionTarget) << 8));
    m.wr8(m.ss(), kActingActor, uint8_t(m.cx() >> 8));
    m.wr8(m.ss(), kActionTarget, uint8_t(m.cx()));
    m.set_al(uint8_t(m.cx()));
    battle_actor_pointer_core(m);
    m.set_al(m.rd8(m.ss(), uint16_t(m.si() + 31)));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        party_slot_for_name_id_core(m);
        m.cs() = caller_cs;
    };
    m.wr8(m.ss(), kTargetGroup, m.al());
    m.cx() = uint16_t(m.rd8(m.ss(), kActionId) | (m.rd8(m.ss(), kActionEffect) << 8));
    m.wr8(m.ss(), kActionId, uint8_t(m.cx() >> 8));
    m.wr8(m.ss(), kActionEffect, uint8_t(m.cx()));
    m.cx() = cx;
    m.si() = si;
    return;
}

// Shared body of ROM 0x363434
// Position the ally-target cursor at one of four actor rows and upload its marker/display state.
void position_ally_target_cursor_core(Machine &m) {
    m.ax() = 0x90;
    m.wr16(m.ss(), (kSpritePositions + 0x20), m.ax());
    m.ax() = 0;
    m.ds() = 0;
    m.bx() = m.rd8(m.ss(), kBattleMember);
    m.set_al(m.rd8(m.ss(), uint16_t(kTargetSlot + m.bx())));
    uint16_t row_pixel_offset = m.al() == 0 ? 0 : m.al() == 1 ? 0x3800 : m.al() == 2 ? 0x7000 : 0xA800;
    m.ax() = 0;
    m.ds() = 0;
    for (uint16_t sprite_index = 0; sprite_index < 4; ++sprite_index) {
        m.ax() = uint16_t(row_pixel_offset + ((sprite_index & 1) ? 0x800 : 0) +
                          ((sprite_index & 2) ? 0x7C : 0x74));
        m.wr16(m.ss(), uint16_t((kSpritePositions + 0x10) + sprite_index * 4), m.ax());
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        draw_battle_cursor_tiles_alt_core(m);
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        draw_battle_marker_sprite_core(m);
        m.cs() = caller_cs;
    }
    return;
}

// Shared body of ROM 0x365F5C
// Upload debug-menu sprites/palettes, select four sprites and wait a VBlank.
void upload_debug_menu_frame_core(Machine &m) {
    dma_sprite_table_core(m);
    dma_palettes_core(m);
    m.set_al(0);
    m.out8(5, 0);
    m.set_al(4);
    m.out8(6, m.al());
    m.call_far(0x4000, 9);
    return;
}

// rom: 0x365F5C (near)
// Upload debug-menu sprites/palettes, select four sprites and wait a VBlank.
void upload_debug_menu_frame(Machine &m) {
    upload_debug_menu_frame_core(m);
    m.ret();
}
// rom: 0x3669B1 (near)
// Display the action announcement once unless actor/menu flags suppress it.
void announce_battle_action_once_core(Machine &m) {
    m.set_al(m.rd8(m.ss(), kBattleFlashCounter));
    if (!m.al()) {
        m.set_al(m.rd8(m.ss(), kActingActor));
        bool should_announce;
        if (m.al() < 4) {
            m.bx() = m.al();
            m.set_al(m.rd8(m.ss(), uint16_t(kMemberSkipFlags + m.bx())));
            should_announce = m.al() != 0xFF;
        } else {
            m.set_al(m.rd8(m.ss(), kBattleMode));
            should_announce = m.al() != 2;
        }
        if (should_announce)
            show_battle_actor_failure_core(m);
        m.set_al(uint8_t(m.rd8(m.ss(), kBattleFlashCounter) + 1));
        m.wr8(m.ss(), kBattleFlashCounter, m.al());
    }
    return;
}

void announce_battle_action_once(Machine &m) {
    announce_battle_action_once_core(m);
    m.ret();
}
// Shared body of ROM 0x369DCD
void build_action_target_description_core(Machine &m) {
    battle_save(m);
    m.set_al(m.rd8(m.ss(), kActingActor));
    if (m.al() < 4) {
        m.ax() = m.rd8(m.ss(), kActionTarget);
        battle_push(m, m.di());
        m.di() = kBattleTargetIcons;
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x6000;
            build_status_icons_core(m);
            m.cs() = caller_cs;
        };
        m.di() = battle_pop(m);
    } else {
        m.set_al(m.rd8(m.ss(), kTargetGroup));
        battle_push(m, m.di());
        m.di() = kBattleTargetIcons;
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x6000;
            build_party_name_core(m);
            m.cs() = caller_cs;
        };
        m.di() = battle_pop(m);
    }
    battle_restore(m);
    return;
}

// rom: 0x369DCD (near)
// Build target status icons for a party attacker, or a party-name record for an enemy attacker. Preserve
// registers.
void build_action_target_description(Machine &m) {
    build_action_target_description_core(m);
    m.ret();
}

// rom: 0x361833 (near)
// Handle ally-target selection keys, validating the chosen actor before confirming.
void handle_ally_target_keys_core(Machine &m) {
    battle_push(m, m.ax());
    uint16_t keys = m.rd16(m.ss(), kKeysRepeat);
    if (keys & (0x80 | 0x20)) {
        m.ax() = 0x92;
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = kGameState;
            play_sound_effect_entry_core(m);
            m.cs() = caller_cs;
        }
        m.bx() = m.rd8(m.ss(), kBattleMember);
        m.set_al(m.rd8(m.ss(), uint16_t(kTargetSlot + m.bx())));
        m.set_al(uint8_t((m.al() + ((keys & 0x80) ? 3 : 1)) & 3));
        m.wr8(m.ss(), uint16_t(kTargetSlot + m.bx()), m.al());
        position_ally_target_cursor_core(m);
    } else if (keys & 4) {
        m.bx() = m.rd8(m.ss(), kBattleMember);
        m.set_al(m.rd8(m.ss(), uint16_t(kTargetSlot + m.bx())));
        battle_actor_pointer_core(m);
        m.set_al(m.rd8(m.ss(), uint16_t(m.si() + 2)));
        if (!(m.al() & 3)) {
            m.set_al(m.rd8(m.ss(), uint16_t(kTargetSlot + m.bx())));
            m.wr8(m.ss(), uint16_t(kMemberActionFlags + m.bx()), 1);
            battle_push(m, m.ax());
            m.ax() = 0x93;
            {
                const uint16_t caller_cs = m.cs();
                m.cs() = kGameState;
                play_sound_effect_entry_core(m);
                m.cs() = caller_cs;
            }
            m.ax() = battle_pop(m);
            m.wr8(m.ss(), uint16_t(kMemberTargets + m.bx()), m.al());
            select_next_battle_member_core(m);
        } else {
            m.ax() = 0x2A;
            {
                const uint16_t caller_cs = m.cs();
                m.cs() = kGameState;
                play_sound_effect_entry_core(m);
                m.cs() = caller_cs;
            }
        }
    } else if (keys & 8) {
        m.ax() = 0x94;
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = kGameState;
            play_sound_effect_entry_core(m);
            m.cs() = caller_cs;
        }
        backtrack_battle_menu_core(m);
    }
    m.ax() = battle_pop(m);
    return;
}

void handle_ally_target_keys(Machine &m) {
    handle_ally_target_keys_core(m);
    m.ret();
}
// Shared body of ROM 0x3618F4
// Backtrack the battle menu state and find the preceding actor able to act.
void backtrack_battle_menu_core(Machine &m) {
    m.set_al(m.rd8(m.ss(), kBattleMenuState));
    if (m.al()) {
        bool select_previous_actor = m.al() == 1;
        if (select_previous_actor) {
            m.cx() &= 0xFF00;
            m.set_al(m.rd8(m.ss(), kBattleMember));
            for (;;) {
                m.set_al(uint8_t(m.al() - 1));
                if (m.al() == 0xFF) {
                    m.set_al(0);
                    break;
                }
                battle_actor_pointer_core(m);
                m.dx() = uint16_t((m.dx() & 0xFF00) | m.rd8(m.ss(), uint16_t(m.si() + 2)));
                if (!(uint8_t(m.dx()) & 0xE3))
                    break;
                m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.cx() + 1));
                if (uint8_t(m.cx()) == 4)
                    break;
            }
            m.wr8(m.ss(), kBattleMember, m.al());
        }
        m.bx() = uint8_t(m.rd8(m.ss(), kBattleMenuHistoryCount) - 1);
        m.wr8(m.ss(), kBattleMenuHistoryCount, uint8_t(m.bx()));
        m.set_al(m.rd8(m.ss(), uint16_t(kBattleMenuHistory + m.bx())));
        m.wr8(m.ss(), kBattleMenuState, m.al());
        if (select_previous_actor && m.al() != 1) {
            m.ax() = m.rd8(m.ss(), kBattleMember);
            draw_actor_item_list_core(m);
            m.ax() = m.rd8(m.ss(), kBattleMember);
        }
        battle_menu_dispatcher_core(m);
    }
    return;
}

// rom: 0x3618F4 (near)
void backtrack_battle_menu(Machine &m) {
    backtrack_battle_menu_core(m);
    m.ret();
}
// Shared body of ROM 0x364134
void draw_meat_reward_core(Machine &m) {
    m.ax() = 0;
    m.es() = 0;
    m.di() = kFormationBackTileSource;
    m.ax() = m.rd8(m.ss(), kMeatSourceType);
    build_formation_layer_record_core(m);
    m.ax() = 0x8000;
    m.ds() = m.ax();
    m.si() = 0x2C0D;
    m.cx() = uint16_t((m.cx() & 0xFF00) | m.rd8(m.ss(), kFormationGraphicsIndex));
    while (uint8_t(m.cx())) {
        m.si() = uint16_t(m.si() + 6);
        m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.cx() - 1));
    }
    battle_push(m, m.si());
    m.ax() = m.rd16(m.ds(), m.si());
    m.si() = m.ax();
    m.cx() = 4;
    m.ax() = 0xC0;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        m.cs() = caller_cs;
    }
    m.si() = battle_pop(m);
    battle_push(m, m.si());
    m.ax() = m.rd16(m.ds(), uint16_t(m.si() + 2));
    m.si() = m.ax();
    m.cx() = 1;
    m.ax() = 12;
    m.di() = kPaletteBuffer;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        m.cs() = caller_cs;
    }
    m.si() = battle_pop(m);
    dma_palettes_core(m);
    m.si() = 0x1B9D;
    m.bx() = 0x70D;
    m.cx() = 0x202;
    m.ax() = 1;
    m.dx() = 0x18C0;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        draw_tilemap_offset_core(m);
        m.cs() = caller_cs;
    }
    return;
}

// rom: 0x364134 (near)
// Draw the selected meat reward icon, palette and label.
void draw_meat_reward(Machine &m) {
    draw_meat_reward_core(m);
    m.ret();
}
namespace {
void draw_debug_number(Machine &m, uint16_t source, uint16_t position) {
    m.ax() = 0;
    m.ds() = 0;
    m.ax() = m.rd8(m.ss(), source);
    m.si() = kDigits;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        number_to_digits_entry_core(m);
        m.cs() = caller_cs;
    }
    m.ax() = blank_pad_and_extract_digits_core(m);
    m.bx() = position;
    m.cx() = 1;
    m.wr16(m.ss(), kTextCursorPos, 1);
    m.dx() &= 0xFF00;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        draw_numeral_string_core(m);
        m.cs() = caller_cs;
    };
}
void draw_debug_boolean(Machine &m, uint16_t source, uint16_t position, TextKey on_key) {
    m.ax() = 0x8000;
    m.ds() = m.ax();
    m.si() = 0x1A9D;
    m.set_al(m.rd8(m.ss(), source));
    if (m.al()) {
        const TextPtr label_text = text(m, on_key);
        m.ds() = label_text.segment;
        m.si() = label_text.offset;
    }
    m.bx() = position;
    m.ax() = 1;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        draw_text_core(m);
        m.cs() = caller_cs;
    }
    text_done(m, on_key);
}
} // namespace
// Shared body of ROM 0x365CA1
void draw_debug_encounter_values_core(Machine &m) {
    draw_debug_number(m, kDebugMapId, 0x40E);
    draw_debug_number(m, kDebugBattleId, 0x60E);
    draw_debug_boolean(m, kDebugAutoWin, 0x80E, TextKey::draw_debug_encounter_values_1);
    draw_debug_boolean(m, kDebugSfxEnabled, 0xA0E, TextKey::draw_debug_encounter_values_2);
    return;
}

// rom: 0x365CA1 (near)
// Draw the debug encounter counts and two boolean switches.
void draw_debug_encounter_values(Machine &m) {
    draw_debug_encounter_values_core(m);
    m.ret();
}
// Shared body of ROM 0x365D32
void draw_debug_configuration_values_core(Machine &m) {
    draw_debug_number(m, kDebugPartySlot, 0x40E);
    // Rows 0-2: three-way switch, each value has its own key. Rows 3-9: two-way switch; only the "off"
    // (0x1A9D) side has a key here (the "on"/0x1A98 side's site is handled separately).
    constexpr TextKey row_keys[3][3] = {
        {TextKey::draw_debug_configuration_values_2, TextKey::draw_debug_configuration_values_3,
         TextKey::draw_debug_configuration_values_1},
        {TextKey::draw_debug_configuration_values_5, TextKey::draw_debug_configuration_values_6,
         TextKey::draw_debug_configuration_values_4},
        {TextKey::draw_debug_configuration_values_8, TextKey::draw_debug_configuration_values_9,
         TextKey::draw_debug_configuration_values_7},
    };
    constexpr TextKey off_keys[7] = {
        TextKey::draw_debug_configuration_values_10, TextKey::draw_debug_configuration_values_11,
        TextKey::draw_debug_configuration_values_12, TextKey::draw_debug_configuration_values_13,
        TextKey::draw_debug_configuration_values_14, TextKey::draw_debug_configuration_values_15,
        TextKey::draw_debug_configuration_values_16,
    };
    for (uint16_t row_index = 0; row_index < 10; ++row_index) {
        m.ax() = 0x8000;
        m.ds() = m.ax();
        m.bx() = row_index;
        m.set_al(m.rd8(m.ss(), kDebugPartySlot));
        m.set_ah(10);
        m.ax() = uint16_t(m.al() * 10);
        m.bx() = uint16_t(m.bx() + m.ax());
        m.set_al(m.rd8(m.ss(), uint16_t(kDebugStatusRecords + m.bx())));
        bool has_text_key = true;
        TextKey label_key = TextKey::draw_debug_configuration_values_1;
        if (row_index < 3) {
            label_key = row_keys[row_index][m.al() == 0 ? 0 : m.al() == 1 ? 1 : 2];
        } else if (m.al() == 1) {
            m.si() = 0x1A98;
            has_text_key = false;
        } else {
            label_key = off_keys[row_index - 3];
        }
        if (has_text_key) {
            const TextPtr label_text = text(m, label_key);
            m.ds() = label_text.segment;
            m.si() = label_text.offset;
        }
        m.bx() = uint16_t(0x50E + row_index * 0x100);
        m.ax() = 1;
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x4000;
            draw_text_core(m);
            m.cs() = caller_cs;
        }
        if (has_text_key)
            text_done(m, label_key);
    }
    return;
}

// rom: 0x365D32 (near)
// Draw the selected debug configuration row and its ten switches.
void draw_debug_configuration_values(Machine &m) {
    draw_debug_configuration_values_core(m);
    m.ret();
}
// rom: 0x366F80 (near)
// Apply the countering target's effective strength as damage after swapping battle sides, then restore the
// sides.
void resolve_counterattack_core(Machine &m) {
    has_table_flag_bit1_core(m);
    if ((m.flags() & emu::CF) && (m.al() & 2)) {
        show_member_item_effect_message_core(m);
        m.set_al(m.rd8(m.ss(), kActionTarget));
        battle_actor_pointer_core(m);
        m.set_al(m.rd8(m.ss(), uint16_t(m.si() + 2)));
        m.set_ah(m.al());
        m.set_al(m.rd8(m.ss(), uint16_t(m.si() + 7)));
        if (m.ah() & 4)
            m.set_al(uint8_t(m.al() >> 1));
        // Counter damage is target strength, halved by weakness (status bit 2).
        m.set_ah(0);
        m.dx() = m.ax();
        m.wr16(m.ss(), kMeterInputTarget, m.dx());
        swap_battle_action_sides_core(m);
        m.set_al(m.rd8(m.ss(), kActionTarget));
        if (m.al() >= 4) {
            m.set_al(m.rd8(m.ss(), kActionId));
            if (m.al() == 0x33 || m.al() == 0x79) {
                m.wr8(m.ss(), kEffectPaletteVariant, m.al() == 0x33 ? 4 : 3);
                {
                    const uint16_t caller_cs = m.cs();
                    m.cs() = 0x7000;
                    (m.al() == 0x33 ? play_hit_sprite_effect_core : play_impact_sprite_effect_core)(m);
                    m.cs() = caller_cs;
                };
            }
        }
        set_battle_meter_target_core(m);
        open_party_member_popup_core(m);
        m.wr8(m.ss(), kBattleKnockoutMode, 1);
        check_target_knocked_out_core(m);
        m.wr8(m.ss(), kBattleKnockoutMode, 0);
        swap_battle_action_sides_core(m);
    }
    return;
}

void resolve_counterattack(Machine &m) {
    resolve_counterattack_core(m);
    m.ret();
}

namespace {
// Append one battle-message fragment, using the shared queue implementation.
void battle_message_fragment(Machine &m, uint16_t segment, uint16_t text, bool first) {
    m.ax() = segment;
    m.ds() = segment;
    m.si() = text;
    m.ax() = 0xFF;
    m.bx() = first ? 0 : 0xFF;
    append_word_queue_core(m);
}
void battle_message_fragment(Machine &m, TextKey key, bool first) {
    const TextPtr message_text = text(m, key);
    m.ax() = message_text.segment;
    m.ds() = message_text.segment;
    m.si() = message_text.offset;
    m.ax() = 0xFF;
    m.bx() = first ? 0 : 0xFF;
    append_word_queue_core(m);
    text_done(m, key);
}
} // namespace
// Shared body of ROM 0x36A475
void append_target_group_suffix_core(Machine &m) {
    battle_save(m);
    m.set_al(m.rd8(m.ss(), kActionTarget));
    if (m.al() >= 4) {
        m.set_al(m.rd8(m.ss(), kFooterPanelMode));
        if (m.al()) {
            m.set_al(m.rd8(m.ss(), kEnemyCount));
            if (m.al() != 1) {
                m.dx() = uint8_t(m.rd8(m.ss(), kItemUseCount) + 1);
                battle_message_fragment(m, TextKey::append_target_group_suffix_1, false);
                m.ax() = 0;
                m.ds() = 0;
                m.ax() = m.dx();
                format_number_core(m);
                battle_message_fragment(m, 0, kBattleNumberText, false);
                battle_message_fragment(m, TextKey::append_target_group_suffix_2, false);
            }
        }
    }
    battle_message_fragment(m, TextKey::append_target_group_suffix_3, false);
    battle_restore(m);
    return;
}

// rom: 0x36A475 (near)
// Append the enemy-group ordinal when required, followed by the action-message separator.
void append_target_group_suffix(Machine &m) {
    append_target_group_suffix_core(m);
    m.ret();
}

namespace {

// All of these messages share a saved register frame and the RAM word queue.
void battle_message_queue(Machine &m, uint16_t segment, uint16_t text, uint16_t width) {
    m.ax() = segment;
    m.ds() = m.ax();
    m.si() = text;
    m.ax() = 0xFF;
    m.bx() = width;
    append_word_queue_core(m);
}
void battle_message_queue(Machine &m, TextKey key, uint16_t width) {
    const TextPtr message_text = text(m, key);
    m.ax() = message_text.segment;
    m.ds() = m.ax();
    m.si() = message_text.offset;
    m.ax() = 0xFF;
    m.bx() = width;
    append_word_queue_core(m);
    text_done(m, key);
}
void battle_message_popup(Machine &m) {
    m.ax() = 0;
    m.es() = 0;
    m.si() = kIconQueue;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        m.cs() = caller_cs;
    };
    // C4D7 is a PUSHA/segment-save immediately followed by the matching restores.
}
void battle_actor_failure_message(Machine &m, uint16_t message_offset, uint16_t suffix_offset = 0) {
    battle_save(m);
    build_battle_member_name_or_icons_core(m);
    battle_message_queue(m, 0, kBattleActorIcons, 0);
    battle_message_queue(m, 0x8000, message_offset, 0xFF);
    if (suffix_offset)
        battle_message_queue(m, 0x8000, suffix_offset, 0xFF);
    battle_message_popup(m);
    battle_restore(m);
}
} // namespace
// Shared body of ROM 0x36B337
// Display that the acting actor's poison ended.
void show_battle_status_resisted_core(Machine &m) {
    battle_actor_failure_message(m, 0x3068, 0x359A);
    return;
}

// Shared body of ROM 0x360CE2
// Roll poison recovery for actor CL; otherwise apply max HP / 16 damage and check for a knockout.
void resolve_battle_status_or_damage_core(Machine &m) {
    battle_push(m, m.bx());
    battle_push(m, m.cx());
    m.set_al(uint8_t(m.cx()));
    battle_actor_pointer_core(m);
    m.dx() = uint16_t((m.dx() & 0xFF) | (uint16_t(m.rd8(m.ss(), uint16_t(m.si() + 10))) << 8));
    // Recover when a 0..100 roll is below actor magic; otherwise poison deals max HP >> 4.
    m.set_al(0);
    m.set_ah(100);
    random_range_core(m);
    if (m.al() < uint8_t(m.dx() >> 8)) {
        m.set_al(m.rd8(m.ss(), uint16_t(m.si() + 2)));
        m.dx() = uint16_t((m.dx() & 0xFF00) | m.al());
        m.set_al(uint8_t(m.al() & 0xEF));
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
        battle_push(m, m.si());
        battle_actor_pointer_core(m);
        m.set_al(m.rd8(m.ss(), uint16_t(m.si() + 0x1F)));
        m.si() = battle_pop(m);
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x6000;
            party_slot_for_name_id_core(m);
            m.cs() = caller_cs;
        };
        m.wr8(m.ss(), kTargetGroup, m.al());
        m.wr8(m.ss(), kActingActor, uint8_t(m.cx()));
        m.wr8(m.ss(), kActionTarget, uint8_t(m.cx()));
        show_actor_damage_pages_core(m);
        m.wr8(m.ss(), kActionId, 0);
        m.dx() = m.rd16(m.ss(), kMeterInputTarget);
        set_battle_meter_target_core(m);
        m.set_al(1);
        m.wr8(m.ss(), kBattleKnockoutMode, m.al());
        check_target_knocked_out_core(m);
    }
    m.cx() = battle_pop(m);
    m.bx() = battle_pop(m);
    return;
}

namespace {
void transformation_text(Machine &m, TextKey key, uint16_t position) {
    const TextPtr message_text = text(m, key);
    m.ax() = message_text.segment;
    m.ds() = message_text.segment;
    m.si() = message_text.offset;
    m.bx() = position;
    m.ax() = 0;
    draw_text_core(m);
    text_done(m, key);
}
void transformation_window(Machine &m, uint16_t position, uint16_t size, bool alternate = false) {
    m.ax() = 0;
    m.bx() = position;
    m.cx() = size;
    if (alternate)
        draw_window_alt_core(m);
    else
        draw_window_core(m);
}
void transformation_confirmation(Machine &m, TextKey yes_key, TextKey no_key) {
    transformation_window(m, 0x90B, 0x0408);
    transformation_text(m, yes_key, 0xA0D);
    transformation_text(m, no_key, 0xB0D);
}
void transformation_stats(Machine &m, uint16_t stat_address, uint8_t x, const TextKey labels[4]) {
    for (uint16_t stat_index = 0; stat_index < 4; ++stat_index) {
        transformation_text(m, labels[stat_index], uint16_t(((9 + stat_index) << 8) | x));
        m.ax() = 0;
        m.ds() = 0;
        m.set_al(m.rd8(m.ss(), uint16_t(stat_address + stat_index)));
        m.si() = kDigits;
        number_to_digits_entry_core(m);
        m.ax() = blank_pad_and_extract_digits_core(m);
        m.cx() = 0;
        m.wr16(m.ss(), kTextCursorPos, 0);
        m.bx() = uint16_t(((9 + stat_index) << 8) | uint8_t(x + 4));
        m.dx() = uint16_t(m.dx() & 0xFF00);
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x6000;
            draw_numeral_string_core(m);
            m.cs() = caller_cs;
        };
    }
}
void transformation_name(Machine &m, uint16_t type_address, uint16_t position) {
    m.set_al(m.rd8(m.ss(), type_address));
    m.di() = kBattleTargetIcons;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        build_name_string_core(m);
        m.cs() = caller_cs;
    };
    m.ax() = 0;
    m.es() = 0;
    m.ax() = 0x9000;
    m.ds() = m.ax();
    m.si() = kBattleTargetIcons;
    m.bx() = position;
    m.cx() = 0;
    draw_status_icons_core(m);
}
void transformation_record(Machine &m, uint16_t type_address) {
    m.bx() = uint16_t(m.rd8(m.ss(), type_address) * 2);
    m.ax() = 0x6000;
    m.es() = m.ax();
    m.si() = kBattleTypePointers;
    m.ax() = table_at16(m, Table::monsters, m.es(), uint16_t(m.bx() + m.si()));
    m.si() = m.ax();
    m.dx() = 0;
}
void transformation_record_stats(Machine &m, uint16_t stat_destination) {
    for (uint16_t stat_index = 0; stat_index < 4; ++stat_index) {
        m.dx() = uint16_t((m.dx() & 0xFF00) |
                          table_at(m, Table::monsters, m.es(), uint16_t(m.si() + 5 + stat_index)));
        m.wr8(m.ss(), uint16_t(stat_destination + stat_index), uint8_t(m.dx()));
    }
}
} // namespace
// Shared body of ROM 0x36419F
// Draw transformation state AL: FF initial prompt, nonzero confirm, zero old/new actor comparison.
void draw_transformation_menu_state_core(Machine &m) {
    if (m.al() == 0xFF) {
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x6000;
            clear_battle_inner_pane_core(m);
            m.cs() = caller_cs;
        };
        draw_meat_reward_core(m);
        m.set_al(0);
        m.wr8(m.ss(), kPopupState, 0);
        m.set_al(m.rd8(m.ss(), kMeatSourceType));
        show_transformation_item_prompt_core(m);
    } else if (m.al() != 0) {
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x6000;
            clear_battle_inner_pane_core(m);
            m.cs() = caller_cs;
        };
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x6000;
            load_formation_set_core(m);
            m.cs() = caller_cs;
        };
        draw_meat_reward_core(m);
        transformation_window(m, 0xA01, 0x041C, true);
        transformation_confirmation(m, TextKey::draw_transformation_menu_state_1,
                                    TextKey::draw_transformation_menu_state_2);
        constexpr uint16_t positions[] = {0x5050, 0x5850, 0x5058, 0x5858};
        for (uint16_t sprite_index = 0; sprite_index < 4; ++sprite_index) {
            m.ax() = positions[sprite_index];
            m.wr16(m.ss(), uint16_t(kSpritePositions + sprite_index * 4), m.ax());
        }
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x6000;
            draw_battle_cursor_tiles_core(m);
            m.cs() = caller_cs;
        }
    } else {
        m.set_al(0);
        m.wr8(m.ss(), kTransformationUnchanged, 0);
        m.set_al(m.rd8(m.ss(), kTransformationMember));
        battle_actor_pointer_core(m);
        m.set_al(m.rd8(m.ss(), uint16_t(m.si() + 0x1F)));
        m.bx() = uint16_t((m.bx() & 0xFF00) | m.al());
        m.set_ah(m.rd8(m.ss(), kMeatSourceType));
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x4000;
            lookup_meat_transformation_core(m);
            m.cs() = caller_cs;
        };
        // Family-pair table selects the result family; the higher current/meat rank selects its type.
        // FFh or the current type means no transformation; retain the old type for the preview.
        if (m.al() == 0xFF || m.al() == uint8_t(m.bx())) {
            m.set_al(1);
            m.wr8(m.ss(), kTransformationUnchanged, m.al());
            m.set_al(uint8_t(m.bx()));
        }
        m.wr8(m.ss(), kNewActorType, m.al());
        m.set_al(m.rd8(m.ss(), kBattleOptions));
        if (m.al() & 8) {
            {
                const uint16_t caller_cs = m.cs();
                m.cs() = 0x6000;
                clear_battle_left_pane_core(m);
                m.cs() = caller_cs;
            };
            {
                const uint16_t caller_cs = m.cs();
                m.cs() = 0x6000;
                clear_battle_right_pane_core(m);
                m.cs() = caller_cs;
            };
            transformation_window(m, 0x13, 0x0E0A);
            transformation_window(m, 1, 0x0E0A);
            m.ax() = 0;
            m.es() = 0;
            m.di() = kFormationMiddleTileSource;
            m.set_al(m.rd8(m.ss(), kTransformationMember));
            battle_actor_pointer_core(m);
            m.si() = uint16_t(m.si() + 0x1F);
            m.set_al(m.rd8(m.ss(), m.si()));
            m.wr8(m.ss(), kOldActorType, m.al());
            transformation_record(m, kOldActorType);
            transformation_record_stats(m, kTransformationOldStats);
            m.set_al(m.rd8(m.ss(), kOldActorType));
            build_formation_layer_record_core(m);
            m.ax() = 0;
            m.es() = 0;
            m.di() = kFormationBackTileSource;
            m.set_al(m.rd8(m.ss(), kNewActorType));
            build_formation_layer_record_core(m);
            {
                const uint16_t native_caller_cs = m.cs();
                m.cs() = 0x6000;
                draw_formation_comparison_portraits_core(m);
                m.cs() = native_caller_cs;
            }
            transformation_confirmation(m, TextKey::draw_transformation_menu_state_3,
                                        TextKey::draw_transformation_menu_state_4);
            transformation_name(m, kOldActorType, 0x802);
            transformation_text(m, TextKey::draw_transformation_menu_state_5, 0x704);
            {
                constexpr TextKey labels[4] = {
                    TextKey::draw_transformation_menu_state_6, TextKey::draw_transformation_menu_state_8,
                    TextKey::draw_transformation_menu_state_7, TextKey::draw_transformation_menu_state_9};
                transformation_stats(m, kTransformationOldStats, 3, labels);
            }
            toggle_meat_menu_cursor_core(m);
            {
                const uint16_t caller_cs = m.cs();
                m.cs() = 0x6000;
                present_battle_frame_scrolled_core(m);
                m.cs() = caller_cs;
            };
            transformation_name(m, kNewActorType, 0x814);
            transformation_record(m, kNewActorType);
            m.dx() = uint16_t(table_at(m, Table::monsters, m.es(), uint16_t(m.si() + 2)));
            m.wr8(m.ss(), kTransformationNewAbilityCount, uint8_t(m.dx()));
            m.dx() = table_at16(m, Table::monsters, m.es(), uint16_t(m.si() + 3));
            m.wr16(m.ss(), kTransformationNewHp, m.dx());
            transformation_record_stats(m, kTransformationNewStats);
            m.cx() = uint16_t((m.cx() & 0xFF00) | 8);
            m.bx() = 0;
            while (uint8_t(m.cx())) {
                m.wr8(m.ss(), uint16_t(m.bx() + kTransformationNewAbilities), 0xFF);
                m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.cx() - 1));
                m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.bx() + 1));
            }
            m.cx() = uint16_t((m.cx() & 0xFF00) | m.rd8(m.ss(), kTransformationNewAbilityCount));
            m.bx() = 0;
            while (uint8_t(m.cx())) {
                m.set_al(table_at(m, Table::monsters, m.es(), uint16_t(m.bx() + m.si() + 11)));
                m.wr8(m.ss(), uint16_t(m.bx() + kTransformationNewAbilities), m.al());
                m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.bx() + 1));
                m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.cx() - 1));
            }
            transformation_text(m, TextKey::draw_transformation_menu_state_10, 0x716);
            {
                constexpr TextKey labels[4] = {
                    TextKey::draw_transformation_menu_state_11, TextKey::draw_transformation_menu_state_13,
                    TextKey::draw_transformation_menu_state_12, TextKey::draw_transformation_menu_state_14};
                transformation_stats(m, kTransformationNewStats, 0x15, labels);
            }
        }
    }
    toggle_meat_menu_cursor_core(m);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        present_battle_frame_scrolled_core(m);
        m.cs() = caller_cs;
    };
    return;
}

// rom: 0x36419F (near)
// Draw transformation state AL: FF initial prompt, nonzero confirm, zero old/new actor comparison.
void draw_transformation_menu_state(Machine &m) {
    draw_transformation_menu_state_core(m);
    m.ret();
}

namespace {
void transformation_hide_sprites(Machine &m, uint16_t first_position_address = kSpritePositions,
                                 uint16_t sprite_count = 8) {
    m.ax() = 0x90;
    for (uint16_t sprite_index = 0; sprite_index < sprite_count; ++sprite_index)
        m.wr16(m.ss(), uint16_t(first_position_address + sprite_index * 4), m.ax());
}
void transformation_sound(Machine &m, uint16_t sound) {
    m.ax() = sound;
    trigger_sound_effect_core(m);
}
void transformation_input(Machine &m, uint16_t keys_address) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        present_battle_frame_scrolled_core(m);
        m.cs() = caller_cs;
    };
    m.call_far(0x4000, 0x5C);
    m.ax() = m.rd16(m.ss(), keys_address);
}
// Test the selected persistent member's monster flag (equipment/status field bit 6).
bool transformation_member_can_change(Machine &m) {
    m.si() = kGameState;
    m.cx() = uint16_t((m.cx() & 0xFF00) | m.al());
    while (uint8_t(m.cx())) {
        m.si() = uint16_t(m.si() + 0x1F);
        m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.cx() - 1));
    }
    m.si() = uint16_t(m.si() + 10);
    m.set_al(m.rd8(m.ss(), m.si()));
    return (m.al() & 0x40) != 0;
}
void transformation_draw_selected_member(Machine &m) {
    m.wr8(m.ss(), kBattleMember, 0);
    m.set_al(m.rd8(m.ss(), kTransformationMember));
    m.wr8(m.ss(), kTargetSlot, m.al());
    if (!transformation_member_can_change(m)) {
        m.set_al(2);
        draw_transformation_menu_state_core(m);
    } else {
        m.set_al(0x15);
        m.wr8(m.ss(), kNewActorType, m.al());
        m.set_al(0);
        draw_transformation_menu_state_core(m);
    }
    position_ally_target_cursor_core(m);
}
// Replace the selected monster when the preview changed, record its meat history, and reload party graphics.
void transformation_apply(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        clear_battle_background_core(m);
        m.cs() = caller_cs;
    };
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        load_formation_set_core(m);
        m.cs() = caller_cs;
    };
    transformation_hide_sprites(m);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        present_battle_frame_scrolled_core(m);
        m.cs() = caller_cs;
    };
    transformation_window(m, 0xA01, 0x041C, true);
    m.set_al(m.rd8(m.ss(), kTransformationUnchanged));
    if (m.al() != 1) {
        m.set_al(0);
        m.wr8(m.ss(), kEffectTargetMode, 0);
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x7000;
            play_directional_transformation_effect_core(m);
            m.cs() = caller_cs;
        };
        m.set_al(m.rd8(m.ss(), kTransformationMember));
        m.set_ah(m.rd8(m.ss(), kNewActorType));
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x4000;
            initialize_monster_member_core(m);
            m.cs() = caller_cs;
        };
        m.bx() = uint16_t((m.bx() & 0xFF00) | m.rd8(m.ss(), kTransformationMember));
        m.set_al(m.rd8(m.ss(), kNewActorType));
        m.set_ah(m.rd8(m.ss(), kMeatSourceType));
        {
            const uint16_t native_caller_cs = m.cs();
            m.cs() = 0x4000;
            append_progress_history_core(m);
            m.cs() = native_caller_cs;
        }
        m.si() = kGameState;
        m.ax() = 0x380;
        m.wr16(m.ss(), kActorGraphicsTile, m.ax());
        m.ax() = 8;
        m.wr16(m.ss(), kActorGraphicsPalette, m.ax());
        m.cx() = uint16_t(m.cx() & 0xFF00);
        do {
            battle_push(m, m.si());
            m.set_al(m.rd8(m.ss(), m.si()));
            m.si() = battle_pop(m);
            if (m.al() != 0xFF) {
                m.wr8(m.ss(), kEffectY, uint8_t(m.cx()));
                load_battle_actor_graphics_core(m);
            }
            m.si() = uint16_t(m.si() + 0x1F);
            m.ax() = uint16_t(m.rd16(m.ss(), kActorGraphicsTile) + 16);
            m.wr16(m.ss(), kActorGraphicsTile, m.ax());
            m.ax() = uint16_t(m.rd16(m.ss(), kActorGraphicsPalette) + 1);
            m.wr16(m.ss(), kActorGraphicsPalette, m.ax());
            m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.cx() + 1));
        } while (uint8_t(m.cx()) != 4);
        m.set_al(m.rd8(m.ss(), kNewActorType));
        battle_table_pointer_core(m);
        m.dx() = table_at16(m, Table::monsters, m.es(), uint16_t(m.di() + 3));
        m.set_al(m.rd8(m.ss(), kTransformationMember));
        battle_actor_pointer_core(m);
        m.wr16(m.ss(), uint16_t(m.si() + 3), m.dx());
        battle_save(m);
        m.dx() = uint16_t((m.dx() & 0xFF00) | 15);
        m.set_al(1);
        m.wr8(m.ss(), kPanelSpecialMode, m.al());
        draw_battle_skill_list_core(m);
        battle_restore(m);
        draw_battle_member_panels_core(m);
        m.set_al(0xFF);
        m.wr8(m.ss(), kEffectTargetMode, m.al());
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x7000;
            play_directional_transformation_effect_core(m);
            m.cs() = caller_cs;
        };
    }
    m.set_al(m.rd8(m.ss(), kTransformationUnchanged));
    if (m.al() != 1) {
        m.di() = kBattleOrder;
        m.set_al(m.rd8(m.ss(), kTransformationMember));
        m.wr8(m.ss(), uint16_t(m.di() + 3), m.al());
        m.wr8(m.ss(), kActingActor, m.al());
        m.set_al(0);
        m.wr8(m.ss(), kPopupState, 0);
        m.set_al(m.rd8(m.ss(), kMeatSourceType));
        show_transformation_result_message_core(m);
    } else {
        m.set_al(0);
        m.wr8(m.ss(), kPopupState, 0);
        show_battle_no_effect_core(m);
    }
    do {
        transformation_input(m, kKeysPressed);
    } while (!(m.ax() & 4));
}
} // namespace
// Shared body of ROM 0x364610
// Interactive transformation confirmation, member selection, comparison and animation.
void run_transformation_menu_core(Machine &m) {
    hooks::EventScope presentation(m, hooks::Event::MeatMenu, {1});
    transformation_hide_sprites(m);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        present_battle_frame_scrolled_core(m);
        m.cs() = caller_cs;
    };
    m.set_al(0);
    for (uint16_t state_address :
         {uint16_t(kBattleMenuState), uint16_t(kTransformationStage), uint16_t(kTransformationChoice),
          uint16_t(kBattleMember), uint16_t(kTargetSlot), uint16_t(kNewActorType), uint16_t(kOldActorType)})
        m.wr8(m.ss(), state_address, 0);
    m.cx() = uint16_t(m.cx() & 0xFF00);
    m.si() = (kBattleActors + 0x2);
    do {
        m.set_al(m.rd8(m.ss(), m.si()));
        if (!(m.al() & 3))
            break;
        m.si() = uint16_t(m.si() + 0x20);
        m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.cx() + 1));
    } while (uint8_t(m.cx()) != 4);
    m.wr8(m.ss(), kTransformationMember, uint8_t(m.cx()));
    m.set_al(0xFF);
    draw_transformation_menu_state_core(m);
    do {
        transformation_input(m, kKeysPressed);
    } while (!(m.ax() & 4));
    transformation_sound(m, 0x93);
    bool redraw = true;
    enum class Stage { choice, member };
    Stage stage = Stage::choice;
    for (;;) {
        if (redraw) {
            hooks::emit(hooks::Event::MeatMenu, m, {m.rd8(m.ss(), kTransformationStage) ? 3 : 2});
            m.set_al(1);
            draw_transformation_menu_state_core(m);
            m.set_al(m.rd8(m.ss(), kTransformationStage));
            m.wr8(m.ss(), kTargetSlot, 0);
            if (m.al() != 0) {
                transformation_draw_selected_member(m);
                stage = Stage::member;
            } else
                stage = Stage::choice;
            redraw = false;
        }
        if (stage == Stage::choice) {
            transformation_input(m, kKeysRepeat);
            const Session *session = Session::find(m.wsc());
            if (session && session->meat_b_cancels && (m.ax() & 8) && !(m.ax() & 4)) {
                // Not in the original (B is ignored there): with the host battle UI, B cancels like picking
                // Don't.
                transformation_sound(m, 0x94);
                m.wr8(m.ss(), kTransformationChoice, 1);
                transformation_hide_sprites(m);
                {
                    const uint16_t caller_cs = m.cs();
                    m.cs() = 0x6000;
                    present_battle_frame_scrolled_core(m);
                    m.cs() = caller_cs;
                };
                break;
            }
            if (m.ax() & 4) {
                transformation_sound(m, 0x93);
                m.set_al(m.rd8(m.ss(), kTransformationChoice));
                if (m.al() != 0) {
                    transformation_hide_sprites(m);
                    {
                        const uint16_t caller_cs = m.cs();
                        m.cs() = 0x6000;
                        present_battle_frame_scrolled_core(m);
                        m.cs() = caller_cs;
                    };
                    break;
                }
                m.wr8(m.ss(), kTransformationStage, 1);
                // L_364709 jumps back to L_364691, which plays the confirm sound again before redrawing.
                transformation_sound(m, 0x93);
                redraw = true;
                continue;
            }
            if (m.ax() & (0x10 | 0x40)) {
                transformation_sound(m, 0x92);
                m.set_al(uint8_t(m.rd8(m.ss(), kTransformationChoice) ^ 1));
                m.wr8(m.ss(), kTransformationChoice, m.al());
                const bool lower_choice = m.al() != 0;
                for (uint16_t sprite_index = 0; sprite_index < 4; ++sprite_index) {
                    m.ax() = uint16_t((sprite_index % 2 ? 0x5800 : 0x5000) |
                                      (lower_choice ? 0x58 : 0x50) + (sprite_index / 2) * 8);
                    m.wr16(m.ss(), uint16_t(kSpritePositions + sprite_index * 4), m.ax());
                }
                {
                    const uint16_t caller_cs = m.cs();
                    m.cs() = 0x6000;
                    draw_battle_cursor_tiles_core(m);
                    m.cs() = caller_cs;
                }
            }
            continue;
        }
        toggle_meat_menu_cursor_core(m);
        m.wr8(m.ss(), kBattleMember, 0);
        m.set_al(m.rd8(m.ss(), kTransformationMember));
        m.wr8(m.ss(), kTargetSlot, m.al());
        position_ally_target_cursor_core(m);
        transformation_input(m, kKeysRepeat);
        if (m.ax() & 4) {
            transformation_sound(m, 0x93);
            m.set_al(m.rd8(m.ss(), kTransformationMember));
            if (!transformation_member_can_change(m)) {
                transformation_sound(m, 0x2A);
                hooks::emit(hooks::Event::MeatMenu, m, {5});
                continue;
            }
            hooks::emit(hooks::Event::MeatMenu, m, {4});
            transformation_apply(m);
            break;
        }
        if (m.ax() & 8) {
            transformation_sound(m, 0x94);
            hooks::emit(hooks::Event::MeatMenu, m, {2});
            m.set_al(1);
            draw_transformation_menu_state_core(m);
            m.wr8(m.ss(), kCursorHidden, 1);
            toggle_meat_menu_cursor_core(m);
            m.wr8(m.ss(), kTransformationStage, 0);
            transformation_hide_sprites(m, (kSpritePositions + 0x10), 4);
            {
                const uint16_t caller_cs = m.cs();
                m.cs() = 0x6000;
                present_battle_frame_scrolled_core(m);
                m.cs() = caller_cs;
            };
            stage = Stage::choice;
            continue;
        }
        if (m.ax() & (0x80 | 0x20)) {
            const bool previous_member = (m.ax() & 0x80) != 0;
            transformation_sound(m, 0x92);
            m.set_al(m.rd8(m.ss(), kTransformationMember));
            m.cx() = uint16_t(m.cx() & 0xFF00);
            do {
                m.set_al(uint8_t(m.al() + (previous_member ? -1 : 1)));
                if (m.al() == 0xFF)
                    m.set_al(3);
                else if (m.al() == 4)
                    m.set_al(0);
                m.dx() = uint16_t((m.dx() & 0xFF00) | m.al());
                battle_actor_pointer_core(m);
                m.dx() = uint16_t((m.dx() & 0xFF00) | m.rd8(m.ss(), uint16_t(m.si() + 2)));
                if (!(uint8_t(m.dx()) & 3))
                    break;
                m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.cx() + 1));
            } while (uint8_t(m.cx()) != 4);
            m.wr8(m.ss(), kTransformationMember, m.al());
            transformation_draw_selected_member(m);
        }
    }
    return;
}

// rom: 0x364610 (near)
// Interactive transformation confirmation, member selection, comparison and animation.
void run_transformation_menu(Machine &m) {
    run_transformation_menu_core(m);
    m.ret();
}

namespace {
void debug_out_word(Machine &m, uint8_t port, uint16_t word) {
    m.out8(port, uint8_t(word));
    m.out8(uint8_t(port + 1), uint8_t(word >> 8));
}
} // namespace
// Shared body of ROM 0x365A3D
// Debug configuration screen: battle/map numbers, music/SFX switches and the party-status editor.
void run_debug_configuration_menu_core(Machine &m) {
    m.call_far(0x4000, 9);
    m.ax() = 0;
    debug_out_word(m, 0, m.ax());
    m.ax() = 8;
    debug_out_word(m, 0x10, m.ax());
    m.ax() = 0x400;
    debug_out_word(m, 0x12, m.ax());
    m.ax() = 0x9000;
    m.ds() = m.ax();
    m.ax() = 0x100;
    m.cx() = 0x100;
    m.si() = 0x80;
    copy_tiles_core(m);
    m.ax() = 15;
    m.cx() = 1;
    m.si() = 0;
    m.di() = kPaletteTargetBuffer;
    copy_palettes_core(m);
    m.ax() = 7;
    m.cx() = 1;
    m.si() = 0x20;
    m.di() = kPaletteTargetBuffer;
    copy_palettes_core(m);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        draw_debug_settings_menu_core(m);
        m.cs() = caller_cs;
    };
    m.ax() = 7;
    debug_out_word(m, 0, m.ax());
    fade_in_battle_scene_core(m);
    m.wr8(m.ss(), kDebugMenuRow, 0);
    m.set_al(m.rd8(m.ss(), kBattleDebug));
    m.wr8(m.ss(), kDebugAutoWin, m.al());
    m.set_al(m.rd8(m.ss(), kDebugForceMeatDrop));
    m.wr8(m.ss(), kDebugSfxEnabled, m.al());
    m.wr8(m.ss(), kDebugPartySlot, 0);
    m.bx() = 0;
    do {
        m.wr8(m.ss(), uint16_t(m.bx() + kDebugStatusRecords), 0);
        ++m.bx();
    } while (m.bx() != 40);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        draw_battle_cursor_tiles_core(m);
        m.cs() = caller_cs;
    }
    for (;;) {
        draw_debug_encounter_values_core(m);
        position_debug_settings_cursor_core(m);
        upload_debug_menu_frame_core(m);
        m.call_far(0x4000, 0x5C);
        m.ax() = m.rd16(m.ss(), kKeysRepeat);
        if (m.ax() & 2) {
            m.ax() = m.rd8(m.ss(), kDebugBattleId);
            m.wr16(m.ss(), kSavedSceneSp, m.ax());
            m.ax() = m.rd8(m.ss(), kDebugMapId);
            m.wr16(m.ss(), kSceneIndex, m.ax());
            m.set_al(m.rd8(m.ss(), kDebugAutoWin));
            m.wr8(m.ss(), kBattleDebug, m.al());
            m.set_al(m.rd8(m.ss(), kDebugSfxEnabled));
            m.wr8(m.ss(), kDebugForceMeatDrop, m.al());
            break;
        }
        if (m.ax() & 4) {
            m.set_al(m.rd8(m.ss(), kDebugMenuRow));
            if (m.al() == 4) {
                {
                    const uint16_t native_caller_cs = m.cs();
                    m.cs() = 0x6000;
                    run_debug_party_status_menu_core(m);
                    m.cs() = native_caller_cs;
                }
                {
                    const uint16_t caller_cs = m.cs();
                    m.cs() = 0x6000;
                    draw_debug_settings_menu_core(m);
                    m.cs() = caller_cs;
                };
            } else if (m.al() == 5) {
                m.wr8(m.ss(), kDebugMenuRow, 0xFF);
                break;
            }
            continue;
        }
        if (m.ax() & 0x10) {
            m.set_al(m.rd8(m.ss(), kDebugMenuRow));
            m.set_al(m.al() ? uint8_t(m.al() - 1) : 5);
            m.wr8(m.ss(), kDebugMenuRow, m.al());
            continue;
        }
        if (m.ax() & 0x20) {
            m.set_al(m.rd8(m.ss(), kDebugMenuRow));
            uint8_t row = m.al();
            if (row < 4) {
                const uint16_t address = uint16_t(kDebugMapId + row);
                m.set_al(m.rd8(m.ss(), address));
                if (row == 0) {
                    if (m.al() != 0x2E)
                        m.set_al(uint8_t(m.al() + 1));
                } else if (row == 1)
                    m.set_al(uint8_t(m.al() + 1));
                else
                    m.set_al(uint8_t(m.al() ^ 1));
                m.wr8(m.ss(), address, m.al());
            }
            continue;
        }
        if (m.ax() & 0x40) {
            m.set_al(m.rd8(m.ss(), kDebugMenuRow));
            m.set_al(m.al() == 5 ? 0 : uint8_t(m.al() + 1));
            m.wr8(m.ss(), kDebugMenuRow, m.al());
            continue;
        }
        if (m.ax() & 0x80) {
            m.set_al(m.rd8(m.ss(), kDebugMenuRow));
            uint8_t row = m.al();
            if (row < 4) {
                const uint16_t address = uint16_t(kDebugMapId + row);
                m.set_al(m.rd8(m.ss(), address));
                if (row == 0) {
                    if (m.al())
                        m.set_al(uint8_t(m.al() - 1));
                } else if (row == 1)
                    m.set_al(uint8_t(m.al() - 1));
                else
                    m.set_al(uint8_t(m.al() ^ 1));
                m.wr8(m.ss(), address, m.al());
            }
        }
    }
    fade_all_palettes_to_black_core(m);
    return;
}

// rom: 0x365A3D
// Debug configuration screen: battle/map numbers, music/SFX switches and the party-status editor.
void run_debug_configuration_menu(Machine &m) {
    run_debug_configuration_menu_core(m);
    m.retf();
}

namespace {
void debug_apply_party_status(Machine &m) {
    m.si() = kGameState;
    m.bx() = 0;
    m.cx() = 0;
    while (m.cx() != 4) {
        for (uint16_t stat_index = 0; stat_index < 3; ++stat_index) {
            m.dx() = uint16_t((m.dx() & 0xFF00) | m.rd8(m.ss(), uint16_t(m.bx() + kDebugStatusRecords)));
            battle_push(m, m.bx());
            m.bx() = m.cx();
            m.wr8(m.ss(), uint16_t(kBattleStrengthBonuses + stat_index * 4 + m.bx()),
                  uint8_t(m.dx()) == 1   ? 1
                  : uint8_t(m.dx()) == 2 ? 0xFF
                                         : 0);
            m.bx() = battle_pop(m);
            ++m.bx();
        }
        constexpr uint8_t status_bits[] = {0x10, 0x80, 0x40, 0x20, 8, 4, 2};
        for (uint8_t status_bit : status_bits) {
            m.set_al(m.rd8(m.ss(), m.si()));
            m.dx() = uint16_t((m.dx() & 0xFF00) | m.rd8(m.ss(), uint16_t(m.bx() + kDebugStatusRecords)));
            battle_push(m, m.bx());
            m.bx() = m.cx();
            // Zero leaves an existing status set: the original only ORs requested bits.
            if (uint8_t(m.dx()) == 1)
                m.set_al(uint8_t(m.al() | status_bit));
            m.wr8(m.ss(), m.si(), m.al());
            m.bx() = battle_pop(m);
            ++m.bx();
        }
        m.si() = uint16_t(m.si() + 0x1F);
        ++m.cx();
    }
}
void debug_edit_party_value(Machine &m, bool increase) {
    m.set_al(m.rd8(m.ss(), kDebugListRow));
    uint8_t setting_row = m.al();
    if (setting_row == 0) {
        m.set_al(m.rd8(m.ss(), kDebugPartySlot));
        if (increase ? m.al() != 3 : m.al() != 0) {
            m.set_al(uint8_t(m.al() + (increase ? 1 : -1)));
            m.wr8(m.ss(), kDebugPartySlot, m.al());
        }
    } else if (setting_row <= 10) {
        m.bx() = uint16_t(setting_row - 1);
        m.set_al(m.rd8(m.ss(), kDebugPartySlot));
        m.set_ah(10);
        m.ax() = uint16_t(m.al() * m.ah());
        m.bx() = uint16_t(m.bx() + m.ax());
        m.set_al(m.rd8(m.ss(), uint16_t(m.bx() + kDebugStatusRecords)));
        const uint8_t maximum_value = setting_row <= 3 ? 2 : 1;
        if (increase ? m.al() != maximum_value : m.al() != 0) {
            m.set_al(uint8_t(m.al() + (increase ? 1 : -1)));
            m.wr8(m.ss(), uint16_t(m.bx() + kDebugStatusRecords), m.al());
        }
    }
}
} // namespace
// Shared body of ROM 0x365F74
// Debug party status editor: four ten-byte records; apply encodes tristates and only sets status bits.
void run_debug_party_status_menu_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        draw_debug_extended_menu_core(m);
        m.cs() = caller_cs;
    };
    m.wr8(m.ss(), kDebugListRow, 0);
    for (;;) {
        draw_debug_configuration_values_core(m);
        position_debug_list_cursor_core(m);
        upload_debug_menu_frame_core(m);
        m.call_far(0x4000, 0x5C);
        m.ax() = m.rd16(m.ss(), kKeysRepeat);
        if (m.ax() & 2) {
            debug_apply_party_status(m);
            break;
        }
        if (m.ax() & 4) {
            m.set_al(m.rd8(m.ss(), kDebugListRow));
            if (m.al() == 11) {
                m.bx() = 0;
                do {
                    m.wr8(m.ss(), uint16_t(m.bx() + kDebugStatusRecords), 0);
                    ++m.bx();
                } while (m.bx() != 36);
                break;
            }
        } else if (m.ax() & 0x10) {
            m.set_al(m.rd8(m.ss(), kDebugListRow));
            m.set_al(m.al() ? uint8_t(m.al() - 1) : 11);
            m.wr8(m.ss(), kDebugListRow, m.al());
        } else if (m.ax() & 0x20)
            debug_edit_party_value(m, true);
        else if (m.ax() & 0x40) {
            m.set_al(m.rd8(m.ss(), kDebugListRow));
            m.set_al(m.al() == 11 ? 0 : uint8_t(m.al() + 1));
            m.wr8(m.ss(), kDebugListRow, m.al());
        } else if (m.ax() & 0x80)
            debug_edit_party_value(m, false);
    }
    return;
}

// rom: 0x365F74
// Debug party status editor: four ten-byte records; apply encodes tristates and only sets status bits.
void run_debug_party_status_menu(Machine &m) {
    run_debug_party_status_menu_core(m);
    m.retf();
}

// Shared body of ROM 0x36B0F7
// Display that the actor is paralyzed.
void show_battle_escape_failed_core(Machine &m) {
    battle_actor_failure_message(m, 0x3026);
    return;
}

// Shared body of ROM 0x3669E6
// Roll paralysis recovery using item FEh field 4, clear status 40h on success, and redraw the party.
void attempt_battle_escape_core(Machine &m) {
    m.ax() = 0xFE;
    m.bx() = 4;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        lookup_table_byte_core(m);
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t((m.cx() & 0xFF00) | m.al());
    m.ax() = 0x6400;
    random_range_core(m);
    if (m.al() >= uint8_t(m.cx()))
        show_actor_paralyzed_message_core(m);
    else {
        show_battle_escape_success_core(m);
        m.set_al(m.rd8(m.ss(), kActingActor));
        battle_actor_pointer_core(m);
        m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 2)) & 0xBF));
        BattleActorState(m, m.si()).write_status(m.al());
        battle_save(m);
        m.dx() = uint16_t((m.dx() & 0xFF00) | 15);
        draw_battle_skill_list_core(m);
        battle_restore(m);
        draw_battle_member_panels_core(m);
        m.set_al(0);
        m.out8(5, m.al());
        m.set_al(0);
        m.out8(6, m.al());
        dma_sprite_table_core(m);
        dma_palettes_core(m);
        m.call_far(0x4000, 9);
    }
    return;
}

// Shared body of ROM 0x36B0B5
// Display that the actor is asleep.
void show_battle_disengage_failed_core(Machine &m) {
    battle_actor_failure_message(m, 0x304A);
    return;
}

// Shared body of ROM 0x366A40
// Roll sleep recovery using item FDh field 4, clear status 80h on success, and redraw the party.
void attempt_battle_disengage_core(Machine &m) {
    m.ax() = 0xFD;
    m.bx() = 4;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        lookup_table_byte_core(m);
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t((m.cx() & 0xFF00) | m.al());
    m.ax() = 0x6400;
    random_range_core(m);
    if (m.al() >= uint8_t(m.cx()))
        show_actor_asleep_message_core(m);
    else {
        show_battle_disengage_success_core(m);
        m.set_al(m.rd8(m.ss(), kActingActor));
        battle_actor_pointer_core(m);
        m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 2)) & 0x7F));
        BattleActorState(m, m.si()).write_status(m.al());
        battle_save(m);
        m.dx() = uint16_t((m.dx() & 0xFF00) | 15);
        draw_battle_skill_list_core(m);
        battle_restore(m);
        draw_battle_member_panels_core(m);
        m.set_al(0);
        m.out8(5, m.al());
        m.set_al(0);
        m.out8(6, m.al());
        dma_sprite_table_core(m);
        dma_palettes_core(m);
        m.call_far(0x4000, 9);
    }
    return;
}

namespace {
struct BattleMessagePart {
    uint16_t segment, text, width;
};
void battle_message_parts(Machine &m, std::initializer_list<BattleMessagePart> parts) {
    for (const auto &part : parts)
        battle_message_queue(m, part.segment, part.text, part.width);
    battle_message_popup(m);
}
void battle_effect_skill_frame(Machine &m) {
    battle_save(m);
    m.dx() = uint16_t((m.dx() & 0xFF00) | 15);
    draw_battle_skill_list_core(m);
    battle_restore(m);
    draw_battle_member_panels_core(m);
    m.set_al(0);
    m.out8(5, m.al());
    m.set_al(0);
    m.out8(6, m.al());
    dma_sprite_table_core(m);
    dma_palettes_core(m);
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 9);
}
} // namespace
// Shared body of ROM 0x36AFF9
// Shows "<target> D_383068 <item 0xDBFD's icons> D_383178".
void show_member_item_effect_message_core(Machine &m) {
    battle_save(m);
    build_battle_member_display_core(m);
    m.ax() = m.rd8(m.ss(), kActingActor);
    m.set_ah(m.rd8(m.ss(), kActionEffect));
    m.dx() = uint16_t((m.dx() & 0xFF00) | 1);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        build_icon_list_core(m);
        m.cs() = caller_cs;
    };
    battle_message_queue(m, 0, kBattleTargetIcons, 0);
    battle_message_queue(m, TextKey::show_target_item_message_1, 0xFF); // D_383068
    battle_message_queue(m, 0, kBattleItemIcons, 0xFF);
    battle_message_queue(m, TextKey::show_target_item_message_2, 0xFF); // D_383178
    battle_message_popup(m);
    battle_restore(m);
    return;
}

// Shared body of ROM 0x366F2A
// Double pending damage, apply it with actor and target swapped, then restore the sides.
void apply_doubled_battle_damage_core(Machine &m) {
    // Double the pending word with 16-bit wrapping before swapping the damaged side.
    m.dx() = uint16_t(m.rd16(m.ss(), kMeterInputTarget) * 2);
    show_target_item_message_core(m);
    swap_battle_action_sides_core(m);
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
            };
        } else if (m.al() == 0x79) {
            m.wr8(m.ss(), kEffectPaletteVariant, 3);
            {
                const uint16_t caller_cs = m.cs();
                m.cs() = 0x7000;
                play_impact_sprite_effect_core(m);
                m.cs() = caller_cs;
            };
        }
    }
    set_battle_meter_target_core(m);
    open_party_member_popup_core(m);
    m.wr8(m.ss(), kBattleKnockoutMode, 1);
    check_target_knocked_out_core(m);
    m.wr8(m.ss(), kBattleKnockoutMode, 0);
    swap_battle_action_sides_core(m);
    return;
}

// Shared body of ROM 0x36A969
// Show the complete target attack announcement in one popup.
void show_battle_effect_introduction_core(Machine &m) { show_target_attack_message_core(m); }

// Shared body of ROM 0x36A053
// Show the actor and attempted item, followed by the no-effect message.
void show_battle_actor_immunity_core(Machine &m) { show_actor_item_failed_message_core(m); }

// Shared body of ROM 0x366FFD
// Check the acting side's immunity and status threshold, then swap sides to inflict the action status.
void apply_battle_status_effect_core(Machine &m) {
    show_target_attack_message_core(m);
    m.set_al(m.rd8(m.ss(), kActionEffect));
    m.bx() = 5;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        lookup_table_byte_core(m);
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t((m.cx() & 0xFF00) | m.al());
    m.set_al(m.rd8(m.ss(), kActingActor));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 11)) & uint8_t(m.cx())));
    if (m.al())
        show_actor_item_failed_message_core(m);
    else {
        m.set_al(m.rd8(m.ss(), kActingActor));
        battle_actor_pointer_core(m);
        m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 12)) & uint8_t(m.cx())));
        if (!m.al())
            m.dx() = 0;
        else {
            m.dx() = uint16_t(m.rd8(m.ss(), uint16_t(m.si() + 10)) + 50);
            m.set_al(m.rd8(m.ss(), kActionTarget));
            battle_actor_pointer_core(m);
            m.set_al(m.rd8(m.ss(), uint16_t(m.si() + 10)));
            // Original quirk: subtract full AX; AH still contains the item-pointer high byte, not zero.
            m.dx() = uint16_t(m.dx() - m.ax());
        }
        m.ax() = 0x6400;
        random_range_core(m);
        m.set_ah(0);
        // Inflict only when the signed threshold is below a 0..100 roll.
        if (int16_t(m.dx()) < int16_t(m.ax())) {
            swap_battle_action_sides_core(m);
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
                    };
                } else if (m.al() == 0xCE) {
                    m.wr8(m.ss(), kEffectPaletteVariant, 1);
                    {
                        const uint16_t caller_cs = m.cs();
                        m.cs() = 0x7000;
                        play_battle_effect_6_core(m);
                        m.cs() = caller_cs;
                    };
                } else if (m.al() == 0xCF) {
                    m.wr8(m.ss(), kEffectPaletteVariant, 5);
                    {
                        const uint16_t caller_cs = m.cs();
                        m.cs() = 0x7000;
                        play_battle_effect_7_core(m);
                        m.cs() = caller_cs;
                    };
                }
            }
            m.set_al(m.rd8(m.ss(), kActionTarget));
            battle_actor_pointer_core(m);
            m.set_al(m.rd8(m.ss(), uint16_t(m.si() + 2)));
            m.dx() = uint16_t((m.dx() & 0xFF00) | m.al());
            m.set_al(m.rd8(m.ss(), kActionId));
            m.bx() = 4;
            {
                const uint16_t caller_cs = m.cs();
                m.cs() = 0x6000;
                lookup_table_byte_core(m);
                m.cs() = caller_cs;
            }
            m.dx() = uint16_t((m.dx() & 0xFF) | (uint16_t(m.al()) << 8));
            if ((uint8_t(m.dx() >> 8) & uint8_t(m.dx())) ||
                !hooks::should(hooks::VB::InflictStatus, m, true,
                               {m.rd8(m.ss(), kActionTarget), uint8_t(m.dx() >> 8)}))
                show_battle_no_effect_core(m);
            else {
                m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(uint8_t(m.dx()) | m.al()));
                BattleActorState(m, m.si()).write_status(uint8_t(m.dx()));
                m.set_al(uint8_t(m.dx() >> 8));
                show_status_inflicted_message_core(m);
                if (uint8_t(m.dx() >> 8) & 3) {
                    if (uint8_t(m.dx() >> 8) & 1) {
                        m.ax() = 0;
                        BattleActorState(m, m.si()).write_hp(m.ax());
                    }
                    m.set_al(uint8_t(m.rd8(m.ss(), kBattleStatusSuccessCount) + 1));
                    m.wr8(m.ss(), kBattleStatusSuccessCount, m.al());
                }
                m.set_al(m.rd8(m.ss(), kActionTarget));
                if (m.al() >= 4)
                    remove_defeated_enemy_group_core(m);
                else
                    shake_screen_for_party_hit_core(m);
                battle_effect_skill_frame(m);
            }
        }
    }
    // Original quirk: this swap also runs when the earlier conditional swap was skipped.
    swap_battle_action_sides_core(m);
    return;
}

// Shared body of ROM 0x367129
// Roll follow-up item damage unless resisted, then apply it with actor and target swapped.
void apply_battle_item_damage_core(Machine &m) {
    show_target_attack_message_core(m);
    m.set_al(m.rd8(m.ss(), kActionEffect));
    m.dx() = uint16_t((m.dx() & 0xFF) | (uint16_t(m.al()) << 8));
    m.bx() = 5;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        lookup_table_byte_core(m);
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t((m.cx() & 0xFF00) | m.al());
    m.set_al(m.rd8(m.ss(), kActingActor));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 11)) & uint8_t(m.cx())));
    if (m.al())
        show_actor_item_failed_message_core(m);
    else {
        m.set_al(m.rd8(m.ss(), kActionEffect));
        m.bx() = 4;
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x6000;
            lookup_table_byte_core(m);
            m.cs() = caller_cs;
        }
        m.dx() = uint16_t((m.dx() & 0xFF) | (uint16_t(m.al()) << 8));
        m.set_al(m.rd8(m.ss(), kActingActor));
        battle_actor_pointer_core(m);
        m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 12)) & uint8_t(m.cx())));
        // Matching field-0Ch bits retain full item power; otherwise roll 0..power (inclusive).
        if (!m.al()) {
            m.set_ah(uint8_t(m.dx() >> 8));
            m.set_al(0);
            random_range_core(m);
        } else
            m.set_al(uint8_t(m.dx() >> 8));
        m.dx() = m.al();
        m.wr16(m.ss(), kMeterInputTarget, m.dx());
        swap_battle_action_sides_core(m);
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
                };
            }
        }
        open_party_member_popup_core(m);
        set_battle_meter_target_core(m);
        m.wr8(m.ss(), kBattleKnockoutMode, 1);
        check_target_knocked_out_core(m);
        m.wr8(m.ss(), kBattleKnockoutMode, 0);
        swap_battle_action_sides_core(m);
    }
    return;
}

// Shared body of ROM 0x36A0E9
// Shows the damage message "<target> D_38347E <damage> D_383268": the damage word 0xDC0B is formatted into
// 0xD66F first.
void show_battle_damage_amount_core(Machine &m) {
    battle_save(m);
    m.ax() = 0;
    m.ds() = 0;
    m.ax() = m.rd16(m.ss(), kMeterInputTarget);
    format_number_core(m);
    build_battle_member_display_core(m);
    battle_message_queue(m, 0x0, kBattleTargetIcons, 0x0);
    battle_message_queue(m, 0x8000, 0x347E, 0xFF);
    battle_message_queue(m, 0x0, kBattleNumberText, 0xFF);
    battle_message_queue(m, 0x8000, 0x3268, 0xFF);
    battle_message_popup(m);
    battle_restore(m);
    return;
}

// Shared body of ROM 0x36A15F
// Show that the enemies have vanished.
void show_battle_damage_footer_core(Machine &m) {
    battle_save(m);
    battle_message_queue(m, 0x8000, 0x3484, 0x0);
    battle_message_popup(m);
    battle_restore(m);
    return;
}

// Shared body of ROM 0x36A36D
// Show the attack-missed message for the current item.
void show_battle_empty_item_message_core(Machine &m) {
    battle_save(m);
    battle_message_queue(m, 0x8000, 0x3152, 0x0);
    battle_message_popup(m);
    battle_restore(m);
    return;
}

// Shared body of ROM 0x36A399
// Show that the attack did no damage.
void show_member_item_count_message_core(Machine &m) {
    battle_save(m);
    build_battle_member_display_core(m);
    battle_message_queue(m, 0x0, kBattleTargetIcons, 0x0);
    battle_save(m);
    queue_item_count_footer_core(m);
    battle_restore(m);
    battle_message_queue(m, 0x8000, 0x315E, 0xFF);
    battle_message_popup(m);
    battle_restore(m);
    return;
}

// Shared body of ROM 0x36A8B2
// Display that the target died.
void show_member_status_notice_core(Machine &m) {
    battle_save(m);
    build_battle_member_display_core(m);
    battle_message_queue(m, 0x0, kBattleTargetIcons, 0x0);
    battle_message_queue(m, 0x8000, 0x3068, 0xFF);
    battle_message_queue(m, 0x8000, 0x33C4, 0xFF);
    battle_message_popup(m);
    battle_restore(m);
    return;
}

// Shared body of ROM 0x36A907
// Show the target name with its introductory battle description.
void show_member_status_notice_pages_core(Machine &m) { show_target_description_message_core(m); }

// Shared body of ROM 0x36A9F1
// Show that the acting actor is confused.
void show_actor_special_status_notice_core(Machine &m) {
    battle_save(m);
    build_battle_member_name_or_icons_core(m);
    battle_message_queue(m, 0x0, kBattleActorIcons, 0x0);
    battle_message_queue(m, 0x8000, 0x3540, 0xFF);
    battle_message_popup(m);
    battle_restore(m);
    return;
}

// Shared body of ROM 0x36AA33
// Show that the acting actor recovered from its status condition.
void show_actor_status_notice_pages_core(Machine &m) { show_actor_recovered_message_core(m); }

// Shared body of ROM 0x36AA95
// Display that the party has been wiped out.
void show_battle_status_icons_notice_core(Machine &m) {
    battle_save(m);
    m.ax() = 0;
    m.di() = kBattleActorIcons;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        build_status_icons_core(m);
        m.cs() = caller_cs;
    };
    battle_message_queue(m, 0x0, kBattleActorIcons, 0x0);
    battle_message_queue(m, 0x8000, 0x339A, 0xFF);
    battle_message_popup(m);
    battle_restore(m);
    return;
}

// Shared body of ROM 0x36AB1B
// Display that the party escaped.
void show_battle_status_icons_result_core(Machine &m) {
    m.ax() = 0;
    m.di() = kBattleActorIcons;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        build_status_icons_core(m);
        m.cs() = caller_cs;
    };
    battle_message_queue(m, 0x0, kBattleActorIcons, 0x0);
    battle_message_queue(m, 0x8000, 0x351E, 0xFF);
    battle_message_popup(m);
    return;
}

// Shared body of ROM 0x36AB9C
// Display that escape is impossible.
void show_battle_status_result_footer_core(Machine &m) {
    battle_message_queue(m, 0x8000, 0x3532, 0x0);
    battle_message_popup(m);
    return;
}

// Shared body of ROM 0x36ABBD
// Show transformation item prompt; leave queue-renderer register effects visible.
void show_transformation_item_prompt_core(Machine &m) {
    m.ax() = m.rd8(m.ss(), kMeatSourceType);
    m.di() = kBattleActorIcons;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        build_name_string_core(m);
        m.cs() = caller_cs;
    };
    battle_message_queue(m, 0x0, kBattleActorIcons, 0x0);
    battle_message_queue(m, TextKey::show_transformation_item_prompt_1, 0xFF);
    battle_message_queue(m, TextKey::show_transformation_item_prompt_2, 0xFF);
    battle_message_popup(m);
    return;
}

// rom: 0x36ABBD (near)
// Show transformation item prompt; leave queue-renderer register effects visible.
void show_transformation_item_prompt(Machine &m) {
    show_transformation_item_prompt_core(m);
    m.ret();
}

// Shared body of ROM 0x36AC12
// Show transformation result message; leave queue-renderer register effects visible.
void show_transformation_result_message_core(Machine &m) {
    build_battle_member_name_or_icons_core(m);
    m.ax() = m.rd8(m.ss(), kNewActorType);
    m.di() = kBattleTargetIcons;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        build_name_string_core(m);
        m.cs() = caller_cs;
    };
    battle_message_queue(m, 0x0, kBattleActorIcons, 0x0);
    battle_message_queue(m, TextKey::show_transformation_result_message_1, 0xFF);
    battle_message_queue(m, 0x0, kBattleTargetIcons, 0xFF);
    battle_message_queue(m, TextKey::show_transformation_result_message_2, 0xFF);
    battle_message_popup(m);
    return;
}

// rom: 0x36AC12 (near)
// Show transformation result message; leave queue-renderer register effects visible.
void show_transformation_result_message(Machine &m) {
    show_transformation_result_message_core(m);
    m.ret();
}

// Shared body of ROM 0x36AC7D
// Display that the actor's paralysis ended.
void show_battle_escape_success_core(Machine &m) {
    battle_save(m);
    build_battle_member_name_or_icons_core(m);
    battle_message_queue(m, 0x0, kBattleActorIcons, 0x0);
    battle_message_queue(m, 0x8000, 0x3038, 0xFF);
    battle_message_popup(m);
    battle_restore(m);
    return;
}

// Shared body of ROM 0x36B073
// Display that the actor woke up.
void show_battle_disengage_success_core(Machine &m) {
    battle_save(m);
    build_battle_member_name_or_icons_core(m);
    battle_message_queue(m, 0x0, kBattleActorIcons, 0x0);
    battle_message_queue(m, 0x8000, 0x3058, 0xFF);
    battle_message_popup(m);
    battle_restore(m);
    return;
}

// Shared body of ROM 0x36B139
// Show battle actor failure; preserve the full register frame.
void show_battle_actor_failure_core(Machine &m) {
    battle_save(m);
    build_battle_member_name_or_icons_core(m);
    battle_message_queue(m, 0x0, kBattleActorIcons, 0x0);
    battle_message_queue(m, TextKey::show_battle_actor_failure_1, 0xFF);
    battle_message_popup(m);
    battle_restore(m);
    return;
}

// rom: 0x36B139 (near)
// Show battle actor failure; preserve the full register frame.
void show_battle_actor_failure(Machine &m) {
    show_battle_actor_failure_core(m);
    m.ret();
}

// Shared body of ROM 0x36B1BA
// Shows D_383288 (no effect).
void show_battle_no_effect_core(Machine &m) {
    battle_save(m);
    battle_message_queue(m, TextKey::show_no_effect_message_1, 0x0); // D_383288
    battle_message_popup(m);
    battle_restore(m);
    return;
}

// Shared body of ROM 0x36B48B
// Show that the target died.
void show_battle_member_count_footer_core(Machine &m) {
    battle_save(m);
    build_battle_member_display_core(m);
    battle_message_queue(m, 0x0, kBattleTargetIcons, 0x0);
    append_target_group_suffix_core(m);
    battle_message_queue(m, 0x8000, 0x3170, 0xFF);
    battle_message_popup(m);
    battle_restore(m);
    return;
}

// Shared body of ROM 0x36B4D0
// Show that the target turned to stone.
void show_member_count_notice_pages1_core(Machine &m) { show_status_bit1_message_core(m); }

// Shared body of ROM 0x36B522
// Show that the target is cursed.
void show_member_count_notice2_core(Machine &m) {
    battle_save(m);
    build_battle_member_display_core(m);
    battle_message_queue(m, 0, kBattleTargetIcons, 0);
    append_target_group_suffix_core(m);
    battle_message_queue(m, 0x8000, 0x33F0, 255);
    battle_message_popup(m);
    battle_restore(m);
    return;
}

// Shared body of ROM 0x36B567
// Show that the target is blinded.
void show_member_count_notice_pages3_core(Machine &m) { show_status_bit3_message_core(m); }

// Shared body of ROM 0x36B5B9
// Show that the target is poisoned.
void show_member_count_notice4_core(Machine &m) {
    battle_save(m);
    build_battle_member_display_core(m);
    battle_message_queue(m, 0, kBattleTargetIcons, 0);
    append_target_group_suffix_core(m);
    battle_message_queue(m, 0x8000, 0x3422, 255);
    battle_message_popup(m);
    battle_restore(m);
    return;
}

// Shared body of ROM 0x36B5FE
// Shows "<actor> D_383614", then "<the number 0xDC0B> D_3830E2" in a second popup.
void show_actor_damage_pages_core(Machine &m) { show_actor_amount_message_core(m); }

// Shared body of ROM 0x36B681
// Show that the target is confused.
void show_member_count_notice6_core(Machine &m) {
    battle_save(m);
    build_battle_member_display_core(m);
    battle_message_queue(m, 0, kBattleTargetIcons, 0);
    append_target_group_suffix_core(m);
    battle_message_queue(m, 0x8000, 0x3412, 255);
    battle_message_popup(m);
    battle_restore(m);
    return;
}

// Shared body of ROM 0x36B6C6
// Show that the target is paralyzed.
void show_member_count_notice7_core(Machine &m) {
    battle_save(m);
    build_battle_member_display_core(m);
    battle_message_queue(m, 0, kBattleTargetIcons, 0);
    append_target_group_suffix_core(m);
    battle_message_queue(m, 0x8000, 0x3436, 255);
    battle_message_popup(m);
    battle_restore(m);
    return;
}

// Shared body of ROM 0x36B70B
// Show that the target is asleep.
void show_member_count_notice8_core(Machine &m) {
    battle_save(m);
    build_battle_member_display_core(m);
    battle_message_queue(m, 0, kBattleTargetIcons, 0);
    append_target_group_suffix_core(m);
    battle_message_queue(m, 0x8000, 0x3442, 255);
    battle_message_popup(m);
    battle_restore(m);
    return;
}

// Shared body of ROM 0x36BD14
// Show the maximum HP growth amount for the selected member.
void show_battle_stat_change_notice1_core(Machine &m) {
    battle_save(m);
    m.set_ah(0);
    m.di() = kBattleActorIcons;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        build_status_icons_core(m);
        m.cs() = caller_cs;
    };
    m.ax() = 0;
    m.ds() = 0;
    m.ax() = m.rd16(m.ss(), kMeterInputTarget);
    format_number_core(m);
    battle_message_parts(m, {{0, kBattleActorIcons, 0},
                             {0x8000, 0x3068, 255},
                             {0x8000, 0x362E, 255},
                             {0x8000, 0x3634, 255},
                             {0, kBattleNumberText, 255},
                             {0x8000, 0x3306, 255}});
    battle_restore(m);
    return;
}

// Shared body of ROM 0x36BDB4
// Show the strength growth amount for the selected member.
void show_battle_stat_change_notice2_core(Machine &m) {
    battle_save(m);
    m.set_ah(0);
    m.di() = kBattleActorIcons;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        build_status_icons_core(m);
        m.cs() = caller_cs;
    };
    m.ax() = 0;
    m.ds() = 0;
    m.ax() = m.rd16(m.ss(), kMeterInputTarget);
    format_number_core(m);
    battle_message_parts(m, {{0, kBattleActorIcons, 0},
                             {0x8000, 0x3068, 255},
                             {0x8000, 0x32CC, 255},
                             {0x8000, 0x36CA, 255},
                             {0, kBattleNumberText, 255},
                             {0x8000, 0x3306, 255}});
    battle_restore(m);
    return;
}

// Shared body of ROM 0x36BE54
// Show the mana growth amount for the selected member.
void show_battle_stat_change_notice3_core(Machine &m) {
    battle_save(m);
    m.set_ah(0);
    m.di() = kBattleActorIcons;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        build_status_icons_core(m);
        m.cs() = caller_cs;
    };
    m.ax() = 0;
    m.ds() = 0;
    m.ax() = m.rd16(m.ss(), kMeterInputTarget);
    format_number_core(m);
    battle_message_parts(m, {{0, kBattleActorIcons, 0},
                             {0x8000, 0x3068, 255},
                             {0x8000, 0x32E4, 255},
                             {0x8000, 0x36CA, 255},
                             {0, kBattleNumberText, 255},
                             {0x8000, 0x3306, 255}});
    battle_restore(m);
    return;
}

// Shared body of ROM 0x36BEF4
// Show the agility growth amount for the selected member.
void show_battle_stat_change_notice4_core(Machine &m) {
    battle_save(m);
    m.set_ah(0);
    m.di() = kBattleActorIcons;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        build_status_icons_core(m);
        m.cs() = caller_cs;
    };
    m.ax() = 0;
    m.ds() = 0;
    m.ax() = m.rd16(m.ss(), kMeterInputTarget);
    format_number_core(m);
    battle_message_parts(m, {{0, kBattleActorIcons, 0},
                             {0x8000, 0x3068, 255},
                             {0x8000, 0x32D8, 255},
                             {0x8000, 0x36CA, 255},
                             {0, kBattleNumberText, 255},
                             {0x8000, 0x3306, 255}});
    battle_restore(m);
    return;
}

// Shared body of ROM 0x36BF94
// Show the defense growth amount for the selected member.
void show_battle_stat_change_notice5_core(Machine &m) {
    battle_save(m);
    m.set_ah(0);
    m.di() = kBattleActorIcons;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        build_status_icons_core(m);
        m.cs() = caller_cs;
    };
    m.ax() = 0;
    m.ds() = 0;
    m.ax() = m.rd16(m.ss(), kMeterInputTarget);
    format_number_core(m);
    battle_message_parts(m, {{0, kBattleActorIcons, 0},
                             {0x8000, 0x3068, 255},
                             {0x8000, 0x32F0, 255},
                             {0x8000, 0x36CA, 255},
                             {0, kBattleNumberText, 255},
                             {0x8000, 0x3306, 255}});
    battle_restore(m);
    return;
}

// Shared body of ROM 0x36C034
// Show the ability gained by the selected member.
void show_battle_item_change_notice_core(Machine &m) {
    battle_save(m);
    m.set_ah(0);
    m.di() = kBattleActorIcons;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        build_status_icons_core(m);
        m.cs() = caller_cs;
    };
    m.ax() = 0;
    m.ds() = 0;
    m.ax() = m.rd16(m.ss(), kMeterInputTarget);
    format_number_core(m);
    m.ax() = 0;
    m.set_ah(m.rd8(m.ss(), kActionId));
    m.dx() = uint16_t((m.dx() & 0xFF00) | 1);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        build_icon_list_core(m);
        m.cs() = caller_cs;
    };
    battle_message_parts(m, {{0x0, kBattleActorIcons, 0x0},
                             {0x8000, 0x3068, 0xFF},
                             {0x0, kBattleItemIcons, 0xFF},
                             {0x8000, 0x347E, 0xFF},
                             {0x8000, 0x3652, 0xFF}});
    battle_restore(m);
    return;
}

// Shared body of ROM 0x36C0CF
// Show the old and new abilities when a member replaces an ability.
void show_battle_item_exchange_pages_core(Machine &m) { show_ability_replaced_message_core(m); }

// Shared body of ROM 0x36C1AD
// Show that the selected ability gained uses.
void show_battle_item_stat_increase_core(Machine &m) {
    battle_save(m);
    m.set_ah(0);
    m.di() = kBattleActorIcons;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        build_status_icons_core(m);
        m.cs() = caller_cs;
    };
    m.ax() = 0;
    m.ds() = 0;
    m.ax() = m.rd16(m.ss(), kMeterInputTarget);
    format_number_core(m);
    m.ax() = m.rd8(m.ss(), kActingActor);
    m.set_ah(m.rd8(m.ss(), kActionId));
    m.dx() = uint16_t((m.dx() & 0xFF00) | 1);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        build_icon_list_core(m);
        m.cs() = caller_cs;
    };
    battle_message_parts(m, {{0x0, kBattleActorIcons, 0x0},
                             {0x8000, 0x3342, 0xFF},
                             {0x0, kBattleItemIcons, 0xFF},
                             {0x8000, 0x3634, 0xFF},
                             {0x8000, 0x36D6, 0xFF}});
    battle_restore(m);
    return;
}

// Shared body of ROM 0x36C24C
// Show that the selected ability lost uses.
void show_battle_item_stat_decrease_core(Machine &m) {
    battle_save(m);
    m.set_ah(0);
    m.di() = kBattleActorIcons;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        build_status_icons_core(m);
        m.cs() = caller_cs;
    };
    m.ax() = 0;
    m.ds() = 0;
    m.ax() = m.rd16(m.ss(), kMeterInputTarget);
    format_number_core(m);
    m.ax() = m.rd8(m.ss(), kActingActor);
    m.set_ah(m.rd8(m.ss(), kActionId));
    m.dx() = uint16_t((m.dx() & 0xFF00) | 1);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        build_icon_list_core(m);
        m.cs() = caller_cs;
    };
    battle_message_parts(m, {{0x0, kBattleActorIcons, 0x0},
                             {0x8000, 0x3342, 0xFF},
                             {0x0, kBattleItemIcons, 0xFF},
                             {0x8000, 0x3634, 0xFF},
                             {0x8000, 0x36CE, 0xFF}});
    battle_restore(m);
    return;
}

// Shared body of ROM 0x36C2EB
// Show that the party surprised the enemies.
void show_party_status_summary_core(Machine &m) {
    battle_save(m);
    m.set_al(m.rd8(m.ss(), kDialogueLeader));
    m.di() = kBattleActorIcons;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        build_status_icons_core(m);
        m.cs() = caller_cs;
    };
    battle_message_parts(m, {{0x0, kBattleActorIcons, 0x0}, {0x8000, 0x34EC, 0xFF}});
    battle_restore(m);
    return;
}

// Shared body of ROM 0x36C333
// Show that the enemies surprised the party.
void show_battle_summary_footer_core(Machine &m) {
    battle_save(m);
    battle_message_parts(m, {{0x8000, 0x3508, 0x0}});
    battle_restore(m);
    return;
}

// Shared body of ROM 0x36C3A9
// Display that every member of the target group was defeated.
void show_selected_member_result_core(Machine &m) {
    battle_save(m);
    build_battle_member_display_core(m);
    battle_message_parts(m, {{0x0, kBattleTargetIcons, 0x0}, {0x8000, 0x365C, 0xFF}});
    battle_restore(m);
    return;
}

// Shared body of ROM 0x36C3EB
// Display the target or enemy-group name and the number defeated.
void show_battle_party_result_core(Machine &m) {
    battle_save(m);
    battle_message_parts(m, {{0x8000, 0x3690, 0x0}, {0x8000, 0x365C, 0xFF}});
    battle_restore(m);
    return;
}

namespace {
uint16_t battle_in_word(Machine &m, uint8_t port) {
    return uint16_t(m.in8(port) | (uint16_t(m.in8(uint8_t(port + 1))) << 8));
}
void initialize_transition_rows(Machine &m) {
    m.ax() = 0;
    m.dx() = 0xDF00;
    m.cx() = 80;
    m.si() = kNpcBackup;
    do {
        m.wr16(m.ss(), m.si(), m.ax());
        m.wr16(m.ss(), uint16_t(m.si() + 2), m.dx());
        m.si() = uint16_t(m.si() + 4);
    } while (--m.cx());
}
void transition_blank_screen(Machine &m, uint16_t screen, uint16_t size) {
    m.ax() = screen;
    m.bx() = 0;
    m.cx() = size;
    m.dx() = 0x1FF;
    fill_screen_rect_core(m);
}
} // namespace
// Shared body of ROM 0x36C4E0
// Brighten, install HBlank vector 6000:C681, sweep eighty raster rows, then restore the display and fade.
void run_battle_raster_sweep_core(Machine &m) {
    battle_save(m);
    initialize_transition_rows(m);
    m.cx() = 16;
    do {
        battle_push(m, m.cx());
        m.ax() = 0;
        m.cx() = 4;
        brighten_palette_step_core(m);
        m.call_far(0x4000, 9);
        dma_palettes_core(m);
        m.cx() = battle_pop(m);
    } while (--m.cx());
    m.ax() = 0xFFF;
    m.wr16(m.ss(), kVignetteBackdropColor, m.ax());
    m.ax() = battle_in_word(m, 0);
    battle_push(m, m.ax());
    m.set_ah(0xF0);
    debug_out_word(m, 0, m.ax());
    transition_blank_screen(m, 0, 0xA20);
    m.call_far(0x4000, 9);
    m.ax() = uint16_t(battle_in_word(m, 0) | 0x20);
    debug_out_word(m, 0, m.ax());
    m.ax() = 0;
    debug_out_word(m, 8, m.ax());
    m.ax() = 0x8FDF;
    debug_out_word(m, 10, m.ax());
    m.flags() &= ~emu::IF;
    m.ax() = 1;
    debug_out_word(m, 0xA4, m.ax());
    m.set_al(3);
    m.out8(0xA2, m.al());
    m.ax() = 0;
    m.ds() = 0;
    m.bx() = kHblankTimerInterruptVector;
    m.wr16(m.ds(), m.bx(), 0xC681);
    m.wr16(m.ds(), uint16_t(m.bx() + 2), 0x6000);
    m.set_al(uint8_t(m.in8(0xB2) | 0x80));
    m.out8(0xB2, m.al());
    m.flags() |= emu::IF;
    m.ax() = m.ss();
    m.ds() = m.ax();
    m.es() = m.ax();
    m.si() = 0;
    m.di() = 0x7C0;
    m.cx() = 10;
    do {
        battle_push(m, m.cx());
        battle_push(m, m.si());
        battle_push(m, m.di());
        m.si() = uint16_t(m.si() + kScreenMap2);
        m.di() = uint16_t(m.di() + kScreenMap);
        m.flags() &= ~emu::DF;
        m.cx() = 14;
        do {
            m.wr16(m.es(), m.di(), m.rd16(m.ds(), m.si()));
            m.si() = uint16_t(m.si() + 2);
            m.di() = uint16_t(m.di() + 2);
        } while (--m.cx());
        m.di() = battle_pop(m);
        m.si() = battle_pop(m);
        m.si() = uint16_t((m.si() + 0x40) & 0x7FF);
        m.di() = uint16_t((m.di() + 0x40) & 0x7FF);
        m.cx() = battle_pop(m);
    } while (--m.cx());
    hooks::emit(hooks::Event::BattleDefeatEffect, m, {3, 14 * 8});
    transition_blank_screen(m, 1, 0xA0E);
    transition_blank_screen(m, 1, 0xA0E);
    m.cx() = 1;
    do {
        battle_push(m, m.cx());
        m.ax() = 2;
        if (m.cx() >= 80) {
            m.ax() = 1;
            m.cx() = 80;
        }
        m.dx() = 1;
        if (m.cx() < 40) {
            m.ax() = 4;
            m.dx() = 2;
        }
        battle_push(m, m.ax());
        m.si() = kNpcBackup;
        do {
            m.ax() = uint16_t(m.rd16(m.ss(), m.si()) + m.dx());
            if (m.ax() >= 0x70)
                m.ax() = 0x70;
            m.wr16(m.ss(), m.si(), m.ax());
            m.wr8(m.ss(), uint16_t(m.si() + 2), m.al());
            m.si() = uint16_t(m.si() + 4);
        } while (--m.cx());
        m.cx() = battle_pop(m);
        {
            const uint16_t native_caller_cs = m.cs();
            m.cs() = 0x4000;
            wait_frames_core(m);
            m.cs() = native_caller_cs;
        }
        m.cx() = battle_pop(m);
        ++m.cx();
    } while (m.cx() != 0xD8);
    transition_blank_screen(m, 0, 0xA20);
    transition_blank_screen(m, 1, 0xA20);
    hooks::emit(hooks::Event::BattleDefeatEffect, m, {2});
    m.call_far(0x4000, 9);
    m.flags() &= ~emu::IF;
    m.set_al(0x40);
    m.out8(0xB2, m.al());
    m.flags() |= emu::IF;
    m.ax() = 8;
    debug_out_word(m, 0x10, m.ax());
    m.ax() = 0x400;
    debug_out_word(m, 0x12, m.ax());
    m.set_al(0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        load_formation_set_core(m);
        m.cs() = caller_cs;
    };
    m.cx() = 32;
    {
        const uint16_t native_caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        m.cs() = native_caller_cs;
    }
    m.cx() = 16;
    do {
        battle_push(m, m.cx());
        m.ax() = 0;
        m.cx() = 4;
        palette_step_toward_core(m);
        m.cx() = 4;
        {
            const uint16_t native_caller_cs = m.cs();
            m.cs() = 0x4000;
            wait_frames_core(m);
            m.cs() = native_caller_cs;
        }
        dma_palettes_core(m);
        m.cx() = battle_pop(m);
    } while (--m.cx());
    m.ax() = battle_pop(m);
    debug_out_word(m, 0, m.ax());
    battle_restore(m);
    return;
}

// rom: 0x36C4E0
// Brighten, install HBlank vector 6000:C681, sweep eighty raster rows, then restore the display and fade.
void run_battle_raster_sweep(Machine &m) {
    run_battle_raster_sweep_core(m);
    m.retf();
}

// Shared body of ROM 0x36C6D1
// Install line-compare vector 6000:C7C2 and grow two opposing row shutters with signed saturation.
void run_battle_raster_shutter_core(Machine &m) {
    battle_save(m);
    initialize_transition_rows(m);
    m.call_far(0x4000, 9);
    m.ax() = battle_in_word(m, 0);
    battle_push(m, m.ax());
    m.ax() |= 0x20;
    debug_out_word(m, 0, m.ax());
    m.ax() = 0;
    debug_out_word(m, 8, m.ax());
    m.ax() = 0x8FDF;
    debug_out_word(m, 10, m.ax());
    m.flags() &= ~emu::IF;
    m.set_al(0);
    m.out8(3, m.al());
    m.ax() = 0;
    m.ds() = 0;
    m.bx() = kLineCompareInterruptVector;
    m.wr16(m.ds(), m.bx(), 0xC7C2);
    m.wr16(m.ds(), uint16_t(m.bx() + 2), 0x6000);
    m.set_al(uint8_t(m.in8(0xB2) | 0x10));
    m.out8(0xB2, m.al());
    m.flags() |= emu::IF;
    m.cx() = 1;
    for (;;) {
        battle_push(m, m.cx());
        m.si() = kNpcBackup;
        do {
            m.ax() = m.rd16(m.ss(), m.si());
            if (!m.ax())
                ++m.ax();
            else {
                m.ax() = uint16_t(m.ax() * 2);
                if (m.ax() >= 0x98)
                    m.ax() = 0x98;
            }
            m.wr16(m.ss(), m.si(), m.ax());
            m.dx() = uint16_t(0xDF - m.ax());
            m.wr8(m.ss(), uint16_t(m.si() + 3), uint8_t(m.dx()));
            m.si() = uint16_t(m.si() + 4);
        } while (--m.cx());
        m.cx() = battle_pop(m);
        battle_push(m, m.cx());
        m.si() = (kNpcBackup + 0x13C);
        do {
            m.ax() = m.rd16(m.ss(), m.si());
            if (!m.ax())
                --m.ax();
            else {
                m.ax() = uint16_t(m.ax() * 2);
                if (m.ax() < 0xFF70)
                    m.ax() = 0xFF70;
            }
            m.wr16(m.ss(), m.si(), m.ax());
            m.ax() = uint16_t(-m.ax());
            m.wr8(m.ss(), uint16_t(m.si() + 2), m.al());
            m.si() = uint16_t(m.si() - 4);
        } while (--m.cx());
        m.cx() = 4;
        {
            const uint16_t native_caller_cs = m.cs();
            m.cs() = 0x4000;
            wait_frames_core(m);
            m.cs() = native_caller_cs;
        }
        m.cx() = battle_pop(m);
        ++m.cx();
        if (m.cx() >= 40)
            m.cx() = 40;
        m.si() = uint16_t(m.si() + 4);
        m.ax() = m.rd16(m.ss(), m.si());
        if (m.ax() == 0xFF70)
            break;
    }
    transition_blank_screen(m, 1, 0xA1C);
    hooks::emit(hooks::Event::BattleDefeatEffect, m, {2});
    m.call_far(0x4000, 9);
    m.flags() &= ~emu::IF;
    m.set_al(0x40);
    m.out8(0xB2, m.al());
    m.flags() |= emu::IF;
    m.ax() = battle_pop(m);
    debug_out_word(m, 0, m.ax());
    m.ax() = 8;
    debug_out_word(m, 0x10, m.ax());
    m.ax() = 0x400;
    debug_out_word(m, 0x12, m.ax());
    battle_restore(m);
    return;
}

// rom: 0x36C6D1
// Install line-compare vector 6000:C7C2 and grow two opposing row shutters with signed saturation.
void run_battle_raster_shutter(Machine &m) {
    run_battle_raster_shutter_core(m);
    m.retf();
}

} // namespace sawgod::game

namespace sawgod::game {
namespace {
// Formation records share the field movement workspace.
} // namespace
// rom: 0x362D6B (near)
// Load the target member's normal or petrified palette into the current or target buffer.
void load_target_member_palette_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_2d83;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_2d88;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_2d8d;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_2d92;
branch_2d83:;
    m.bx() = uint16_t(0x8);
    if (m.flags() & emu::ZF)
        goto branch_2d97;
branch_2d88:;
    m.bx() = uint16_t(0x9);
    if (m.flags() & emu::ZF)
        goto branch_2d97;
branch_2d8d:;
    m.bx() = uint16_t(0xA);
    if (m.flags() & emu::ZF)
        goto branch_2d97;
branch_2d92:;
    m.bx() = uint16_t(0xB);
    if (m.flags() & emu::ZF)
        goto branch_2d97;
branch_2d97:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() ^ m.bx())));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.bx() = uint16_t(computed::shift_left<uint16_t>(m, m.bx(), 0x1));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bx() + kMemberPaletteOffsets)));
    m.si() = uint16_t(m.ax());
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bx() + kMemberPaletteSegments)));
    m.ds() = uint16_t(m.ax());
    computed::push(m, m.si());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0002))));
    m.si() = uint16_t(computed::pop(m));
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x2));
    if (!(m.flags() & emu::ZF))
        goto branch_2ddd;
    computed::push(m, m.ax());
    computed::push(m, m.bx());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() ^ m.bx())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bx() + kMemberPaletteVariants))));
    m.bx() = uint16_t(0x20);
    advance_record_pointer_call_core(m);
    m.bx() = uint16_t(computed::pop(m));
    m.ax() = uint16_t(computed::pop(m));
    goto branch_2de1;
branch_2ddd:;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xC0));
branch_2de1:;
    m.cx() = uint16_t(0x1);
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bx() + kMemberPaletteSlots)));
    (void)computed::subtract<uint8_t>(m, uint8_t(m.dx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_2df3;
    m.di() = uint16_t(kPaletteTargetBuffer);
    goto branch_2df6;
branch_2df3:;
    m.di() = uint16_t(kPaletteBuffer);
branch_2df6:;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void load_target_member_palette(Machine &m) {
    load_target_member_palette_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x364107
// Compare a random percentage against attacker minus target agility, showing a miss on failure.
void test_drain_hit_by_agility_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    battle_actor_pointer_core(m);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0009))));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0009)))) << 8));
    // Original byte subtraction wraps when target agility exceeds attacker agility; do not clamp it.
    m.bx() = uint16_t((m.bx() & 0xFF00) |
                      uint8_t(computed::subtract<uint8_t>(m, uint8_t(m.bx()), uint8_t(m.bx() >> 8))));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(uint8_t(m.bx())));
    m.set_al(uint8_t(0x0));
    m.set_ah(uint8_t(0x64));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.cx()));
    if (m.flags() & emu::CF)
        goto branch_4133;
    show_attack_missed_message_call_core(m);
    m.set_al(uint8_t(0xFF));
branch_4133:;
    return;
}
void test_drain_hit_by_agility(Machine &m) {
    test_drain_hit_by_agility_core(m);
    if (m.cs() == 0x6000)
        m.retf();
}
// rom: 0x364AD4
// Apply three diagonal scroll offsets and restore the battle screen position.
void shake_battle_screen_once_diagonally_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.dx() = uint16_t(0x8);
    m.dx() = uint16_t(computed::subtract<uint16_t>(m, m.dx(), 0x303));
    m.ax() = uint16_t(m.dx());
    computed::out_word(m, uint8_t(0x10), m.ax());
    m.dx() = uint16_t(0x400);
    m.dx() = uint16_t(computed::subtract<uint16_t>(m, m.dx(), 0x303));
    m.ax() = uint16_t(m.dx());
    computed::out_word(m, uint8_t(0x12), m.ax());
    m.call_far(0x4000, 0x0009);
    m.dx() = uint16_t(0x8);
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), 0x404));
    m.ax() = uint16_t(m.dx());
    computed::out_word(m, uint8_t(0x10), m.ax());
    m.dx() = uint16_t(0x400);
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), 0x404));
    m.ax() = uint16_t(m.dx());
    computed::out_word(m, uint8_t(0x12), m.ax());
    m.call_far(0x4000, 0x0009);
    m.dx() = uint16_t(0x8);
    m.dx() = uint16_t(computed::subtract<uint16_t>(m, m.dx(), 0x404));
    m.ax() = uint16_t(m.dx());
    computed::out_word(m, uint8_t(0x10), m.ax());
    m.dx() = uint16_t(0x400);
    m.dx() = uint16_t(computed::subtract<uint16_t>(m, m.dx(), 0x404));
    m.ax() = uint16_t(m.dx());
    computed::out_word(m, uint8_t(0x12), m.ax());
    m.call_far(0x4000, 0x0009);
    m.ax() = uint16_t(0x8);
    computed::out_word(m, uint8_t(0x10), m.ax());
    m.ax() = uint16_t(0x400);
    computed::out_word(m, uint8_t(0x12), m.ax());
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void shake_battle_screen_once_diagonally(Machine &m) {
    shake_battle_screen_once_diagonally_core(m);
    if (m.cs() == 0x6000)
        m.retf();
}
// rom: 0x364B38
// Shake both screen layers diagonally over two four-frame cycles.
void shake_battle_screen_repeated_diagonally_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.cx() = uint16_t(0x3);
branch_4b3f:;
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    (void)computed::subtract<uint16_t>(m, m.cx(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_4bc3;
    m.dx() = uint16_t(0x8);
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), 0x303));
    m.ax() = uint16_t(m.dx());
    computed::out_word(m, uint8_t(0x10), m.ax());
    m.dx() = uint16_t(0x400);
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), 0x303));
    m.ax() = uint16_t(m.dx());
    computed::out_word(m, uint8_t(0x12), m.ax());
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.dx() = uint16_t(0x8);
    m.dx() = uint16_t(computed::subtract<uint16_t>(m, m.dx(), 0x303));
    m.ax() = uint16_t(m.dx());
    computed::out_word(m, uint8_t(0x10), m.ax());
    m.dx() = uint16_t(0x400);
    m.dx() = uint16_t(computed::subtract<uint16_t>(m, m.dx(), 0x303));
    m.ax() = uint16_t(m.dx());
    computed::out_word(m, uint8_t(0x12), m.ax());
    m.call_far(0x4000, 0x0009);
    m.dx() = uint16_t(0x8);
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), 0x404));
    m.ax() = uint16_t(m.dx());
    computed::out_word(m, uint8_t(0x10), m.ax());
    m.dx() = uint16_t(0x400);
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), 0x404));
    m.ax() = uint16_t(m.dx());
    computed::out_word(m, uint8_t(0x12), m.ax());
    m.call_far(0x4000, 0x0009);
    m.dx() = uint16_t(0x8);
    m.dx() = uint16_t(computed::subtract<uint16_t>(m, m.dx(), 0x404));
    m.ax() = uint16_t(m.dx());
    computed::out_word(m, uint8_t(0x10), m.ax());
    m.dx() = uint16_t(0x400);
    m.dx() = uint16_t(computed::subtract<uint16_t>(m, m.dx(), 0x404));
    m.ax() = uint16_t(m.dx());
    computed::out_word(m, uint8_t(0x12), m.ax());
    m.call_far(0x4000, 0x0009);
    m.ax() = uint16_t(0x8);
    computed::out_word(m, uint8_t(0x10), m.ax());
    m.ax() = uint16_t(0x400);
    computed::out_word(m, uint8_t(0x12), m.ax());
    goto branch_4b3f;
branch_4bc3:;
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void shake_battle_screen_repeated_diagonally(Machine &m) {
    shake_battle_screen_repeated_diagonally_core(m);
    if (m.cs() == 0x6000)
        m.retf();
}
// rom: 0x364BC8
// Shake both screen layers horizontally over two four-frame cycles.
void shake_battle_screen_repeated_horizontally_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.cx() = uint16_t(0x3);
branch_4bcf:;
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    (void)computed::subtract<uint16_t>(m, m.cx(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_4c4a;
    m.dx() = uint16_t(0x8);
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), 0x3));
    m.ax() = uint16_t(m.dx());
    computed::out_word(m, uint8_t(0x10), m.ax());
    m.dx() = uint16_t(0x400);
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), 0x3));
    m.ax() = uint16_t(m.dx());
    computed::out_word(m, uint8_t(0x12), m.ax());
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.dx() = uint16_t(0x8);
    m.dx() = uint16_t(computed::subtract<uint16_t>(m, m.dx(), 0x3));
    m.ax() = uint16_t(m.dx());
    computed::out_word(m, uint8_t(0x10), m.ax());
    m.dx() = uint16_t(0x400);
    m.dx() = uint16_t(computed::subtract<uint16_t>(m, m.dx(), 0x3));
    m.ax() = uint16_t(m.dx());
    computed::out_word(m, uint8_t(0x12), m.ax());
    m.call_far(0x4000, 0x0009);
    m.dx() = uint16_t(0x8);
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), 0x4));
    m.ax() = uint16_t(m.dx());
    computed::out_word(m, uint8_t(0x10), m.ax());
    m.dx() = uint16_t(0x400);
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), 0x4));
    m.ax() = uint16_t(m.dx());
    computed::out_word(m, uint8_t(0x12), m.ax());
    m.call_far(0x4000, 0x0009);
    m.dx() = uint16_t(0x8);
    m.dx() = uint16_t(computed::subtract<uint16_t>(m, m.dx(), 0x4));
    m.ax() = uint16_t(m.dx());
    computed::out_word(m, uint8_t(0x10), m.ax());
    m.dx() = uint16_t(0x400);
    m.dx() = uint16_t(computed::subtract<uint16_t>(m, m.dx(), 0x4));
    m.ax() = uint16_t(m.dx());
    computed::out_word(m, uint8_t(0x12), m.ax());
    m.call_far(0x4000, 0x0009);
    m.ax() = uint16_t(0x8);
    computed::out_word(m, uint8_t(0x10), m.ax());
    m.ax() = uint16_t(0x400);
    computed::out_word(m, uint8_t(0x12), m.ax());
    goto branch_4bcf;
branch_4c4a:;
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void shake_battle_screen_repeated_horizontally(Machine &m) {
    shake_battle_screen_repeated_horizontally_core(m);
    if (m.cs() == 0x6000)
        m.retf();
}
// rom: 0x364C4F
// Install a sine-wave raster distortion and fade palettes during its 256-frame decay.
void animate_battle_raster_wave_fade_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr16(m.ss(), uint16_t(kRasterWaveSource), uint16_t(m.ax()));
    m.wr8(m.ss(), uint16_t(kRasterWaveOffset), uint8_t(m.al()));
    m.set_al(uint8_t(0xFF));
    m.wr8(m.ss(), uint16_t(kRasterWavePhase), uint8_t(m.al()));
    computed::push(m, m.ds());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x3), m.al());
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.bx() = uint16_t(kLineCompareInterruptVector);
    m.wr16(m.ds(), uint16_t(m.bx()), uint16_t(0x4CBC));
    m.wr16(m.ds(), uint16_t(m.bx() + 0x0002), uint16_t(0x6000));
    m.set_al(uint8_t(m.in8(uint8_t(0xB2))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | 0x10))));
    m.out8(uint8_t(0xB2), m.al());
    m.ds() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(0x100);
branch_4c83:;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_visible_scanline_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    computed::push(m, m.cx());
    m.wr8(m.ss(), uint16_t(kRasterWavePhase), uint8_t(uint8_t(m.cx())));
    m.set_al(uint8_t(0x4));
    m.wr8(m.ss(), uint16_t(kRasterWaveOffset),
          uint8_t(computed::add<uint8_t>(m, m.rd8(m.ss(), uint16_t(kRasterWaveOffset)), m.al())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kRasterWaveOffset))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x3F))));
    if (!(m.flags() & emu::ZF))
        goto branch_4ca8;
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0xF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        step_selected_palettes_down_to_target_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
branch_4ca8:;
    m.call_far(0x4000, 0x0009);
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_4c83;
    m.set_al(uint8_t(m.in8(uint8_t(0xB2))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0xEF))));
    m.out8(uint8_t(0xB2), m.al());
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void animate_battle_raster_wave_fade(Machine &m) {
    animate_battle_raster_wave_fade_core(m);
    if (m.cs() == 0x6000)
        m.retf();
}
// rom: 0x364FBE (near)
// Return the target actor's element-resistance byte in AL while preserving SI.
void read_target_element_resistances_core(Machine &m) {
    computed::push(m, m.si());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x000C))));
    m.si() = uint16_t(computed::pop(m));
    return;
}
void read_target_element_resistances(Machine &m) {
    read_target_element_resistances_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x366700 (near)
// Add a 0..AH random roll and BL to DX, returning CL in AL.
void add_random_and_stat_damage_bonus_core(Machine &m) {
    m.set_al(uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.set_ah(uint8_t(0x0));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.ax()));
    m.set_al(uint8_t(uint8_t(m.bx())));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.ax()));
    m.set_al(uint8_t(uint8_t(m.cx())));
    return;
}
void add_random_and_stat_damage_bonus(Machine &m) {
    add_random_and_stat_damage_bonus_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x366712 (near)
// Add a random roll from 0 through AH to DX.
void add_random_damage_bonus_core(Machine &m) {
    m.set_al(uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.set_ah(uint8_t(0x0));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.ax()));
    return;
}
void add_random_damage_bonus(Machine &m) {
    add_random_damage_bonus_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36671E (near)
// Subtract CL's defense, apply resistance and guarding reductions, and cache nonnegative damage.
void reduce_damage_by_defense_and_guard_core(Machine &m) {
    // Damage = max(DX - CL, 0), then effect resistance and guard each halve it; signed results <1 become
    // zero.
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(uint8_t(m.cx())));
    m.dx() = uint16_t(computed::subtract<uint16_t>(m, m.dx(), m.ax()));
    if (!(m.flags() & emu::CF))
        goto branch_6728;
    m.dx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.dx() ^ m.dx())));
branch_6728:;
    {
        const uint16_t callback_cs = m.cs();
        halve_damage_for_effect_resistance_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x000B))));
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x40));
    if (m.flags() & emu::ZF)
        goto branch_673c;
    m.dx() = uint16_t(computed::shift_right<uint16_t>(m, m.dx(), 0x1));
branch_673c:;
    m.wr16(m.ss(), uint16_t(kMeterInputTarget), uint16_t(m.dx()));
    (void)computed::subtract<uint16_t>(m, m.dx(), 0x1);
    if (bool(m.flags() & emu::SF) == bool(m.flags() & emu::OF))
        goto branch_674e;
    m.dx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.dx() ^ m.dx())));
    m.wr16(m.ss(), uint16_t(kMeterInputTarget), uint16_t(m.dx()));
    return;
branch_674e:;
    m.wr16(m.ss(), uint16_t(kMeterInputTarget), uint16_t(m.dx()));
    return;
}
void reduce_damage_by_defense_and_guard(Machine &m) {
    reduce_damage_by_defense_and_guard_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x366754 (near)
// Halve DX when the current effect's flags require a resistance reduction.
void halve_damage_for_effect_resistance_core(Machine &m) {
    has_table_flag_bit1_core(m);
    if (m.flags() & emu::CF)
        goto branch_675a;
    return;
branch_675a:;
    if (m.flags() & emu::ZF)
        goto branch_675e;
    m.dx() = uint16_t(computed::shift_right<uint16_t>(m, m.dx(), 0x1));
branch_675e:;
    return;
}
void halve_damage_for_effect_resistance(Machine &m) {
    halve_damage_for_effect_resistance_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36675F (near)
// Apply resistance and guarding reductions to signed damage and cache the result.
void reduce_signed_damage_for_guard_core(Machine &m) {
    // Effect resistance uses a logical half; guard uses an arithmetic half on the resulting signed word.
    {
        const uint16_t callback_cs = m.cs();
        halve_damage_for_effect_resistance_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x000B))));
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x40));
    if (m.flags() & emu::ZF)
        goto branch_6773;
    m.dx() = uint16_t(computed::shift_right<uint16_t>(m, m.dx(), 0x1, true));
branch_6773:;
    m.wr16(m.ss(), uint16_t(kMeterInputTarget), uint16_t(m.dx()));
    return;
}
void reduce_signed_damage_for_guard(Machine &m) {
    reduce_signed_damage_for_guard_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x366779 (near)
// Mask the target actor ID with the current effect's fourth table byte.
void test_target_actor_effect_mask_core(Machine &m) {
    computed::push(m, m.bx());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x4);
    lookup_table_byte_core(m);
    m.bx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & uint8_t(m.cx())))));
    return;
}
void test_target_actor_effect_mask(Machine &m) {
    test_target_actor_effect_mask_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x366793 (near)
// Return attacker strength in CL, halved when weakened.
void read_attacker_effective_strength_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0007))));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0002))));
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x4));
    if (m.flags() & emu::ZF)
        goto branch_67aa;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::shift_right<uint8_t>(m, uint8_t(m.cx()), 0x1)));
branch_67aa:;
    return;
}
void read_attacker_effective_strength(Machine &m) {
    read_attacker_effective_strength_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x3667AB (near)
// Add a random half-power bonus to the effect's base damage and cache it.
void roll_base_effect_damage_core(Machine &m) {
    computed::push(m, m.bx());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x4);
    lookup_table_byte_core(m);
    // Damage = item field 4 + roll(0..floor(field 4 / 2)); the upper bound is included.
    m.dx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.dx() ^ m.dx())));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(computed::shift_right<uint8_t>(m, m.al(), 0x1)));
    m.set_ah(uint8_t(m.al()));
    {
        const uint16_t callback_cs = m.cs();
        add_random_damage_bonus_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.wr16(m.ss(), uint16_t(kMeterInputTarget), uint16_t(m.dx()));
    m.bx() = uint16_t(computed::pop(m));
    return;
}
void roll_base_effect_damage(Machine &m) {
    roll_base_effect_damage_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x3667CA (near)
// Roll damage, reduce it by defense and guard, and show the hit or no-damage result.
void apply_base_effect_damage_core(Machine &m) {
    {
        const uint16_t callback_cs = m.cs();
        roll_base_effect_damage_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        reduce_damage_by_defense_and_guard_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    (void)computed::subtract<uint16_t>(m, m.dx(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_67e2;
    if (m.flags() & emu::CF)
        goto branch_67e2;
    set_battle_meter_target_core(m);
    open_party_member_popup_core(m);
    check_target_knocked_out_core(m);
    goto branch_67e5;
branch_67e2:;
    show_no_damage_message_call_core(m);
branch_67e5:;
    return;
}
void apply_base_effect_damage(Machine &m) {
    apply_base_effect_damage_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x3667E6 (near)
// Return target defense in CL, halved when weakened.
void read_target_effective_defense_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0008))));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0002))));
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x4));
    if (m.flags() & emu::ZF)
        goto branch_67fd;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::shift_right<uint8_t>(m, uint8_t(m.cx()), 0x1)));
branch_67fd:;
    return;
}
void read_target_effective_defense(Machine &m) {
    read_target_effective_defense_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x3667FE (near)
// Return target and attacker agility in BL and CL, halved when impaired.
void read_effective_battle_agilities_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0009))));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0002))));
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x8));
    if (m.flags() & emu::ZF)
        goto branch_6813;
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::shift_right<uint8_t>(m, uint8_t(m.bx()), 0x1)));
branch_6813:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    battle_actor_pointer_core(m);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0009))));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0002))));
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x8));
    if (m.flags() & emu::ZF)
        goto branch_6828;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::shift_right<uint8_t>(m, uint8_t(m.cx()), 0x1)));
branch_6828:;
    return;
}
void read_effective_battle_agilities(Machine &m) {
    read_effective_battle_agilities_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x366829 (near)
// Add the effect's random spread to its base power and cache the amount.
void roll_effect_power_and_spread_core(Machine &m) {
    computed::push(m, m.bx());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x4);
    lookup_table_byte_core(m);
    m.bx() = uint16_t(computed::pop(m));
    // Damage = item field 4 + roll(0..field 5), with inclusive bounds.
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.al()));
    m.dx() = uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(0x0)) << 8));
    computed::push(m, m.bx());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x5);
    lookup_table_byte_core(m);
    m.bx() = uint16_t(computed::pop(m));
    m.set_ah(uint8_t(m.al()));
    {
        const uint16_t callback_cs = m.cs();
        add_random_damage_bonus_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.wr16(m.ss(), uint16_t(kMeterInputTarget), uint16_t(m.dx()));
    return;
}
void roll_effect_power_and_spread(Machine &m) {
    roll_effect_power_and_spread_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x366854 (near)
// Redirect reflected actions and report target immunity before resolving an effect.
void prepare_reflected_battle_effect_core(Machine &m) {
    m.set_al(uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kTextColorScratch), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kBattleEffectReflected), uint8_t(0x0));
    has_table_flag_bit1_core(m);
    if (!(m.flags() & emu::CF))
        goto branch_6896;
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x8));
    if (m.flags() & emu::ZF)
        goto branch_6896;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    m.wr8(m.ss(), uint16_t(kBattleReflectionActor), uint8_t(m.al()));
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(kBattleEffectReflected), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionEffect))));
    m.bx() = uint16_t(0x1);
    lookup_table_byte_core(m);
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x80));
    if (m.flags() & emu::ZF)
        goto branch_6896;
    m.wr8(m.ss(), uint16_t(kEffectX), uint8_t(0x1));
    {
        const uint16_t callback_cs = m.cs();
        show_magic_reflected_or_game_over_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    goto branch_689c;
branch_6896:;
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() ^ m.al()))));
    m.wr8(m.ss(), uint16_t(kBattleEffectReflected), uint8_t(m.al()));
branch_689c:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleEffectReflected))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_68c6;
    m.dx() = uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget)))) << 8));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleReflectionActor))));
    m.wr8(m.ss(), uint16_t(kActionTarget), uint8_t(m.al()));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x001F))));
    party_slot_for_name_id_core(m);
    m.wr8(m.ss(), uint16_t(kTargetGroup), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kActingActor), uint8_t(uint8_t(m.dx() >> 8)));
branch_68c6:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x5);
    lookup_table_byte_core(m);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x000B))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & uint8_t(m.cx())))));
    if (!(m.flags() & emu::ZF))
        goto branch_68e4;
    return;
branch_68e4:;
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(kTextColorScratch), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_68fb;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_68fb;
    return;
branch_68fb:;
    {
        const uint16_t callback_cs = m.cs();
        show_element_resistance_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    return;
}
void prepare_reflected_battle_effect(Machine &m) {
    prepare_reflected_battle_effect_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x3668FF (near)
// Return nonnegative target-minus-attacker agility with the effect's conditional accuracy adjustment.
void compute_agility_hit_threshold_core(Machine &m) {
    {
        const uint16_t callback_cs = m.cs();
        read_effective_battle_agilities_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    has_table_flag_bit1_core(m);
    if (!(m.flags() & emu::CF))
        goto branch_6919;
    if (m.flags() & emu::ZF)
        goto branch_6919;
    computed::push(m, m.bx());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x5);
    lookup_table_byte_core(m);
    m.bx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::add<uint8_t>(m, uint8_t(m.bx()), m.al())));
    // Threshold = max(byte(target agility + conditional adjustment) - attacker agility, 0).
branch_6919:;
    m.set_al(uint8_t(uint8_t(m.bx())));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), uint8_t(m.cx()))));
    if (m.flags() & emu::CF)
        goto branch_6920;
    return;
branch_6920:;
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() ^ m.al()))));
    return;
}
void compute_agility_hit_threshold(Machine &m) {
    compute_agility_hit_threshold_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x366923 (near)
// Set the immune flag and show the resistance message when the target rejects the effect.
void check_battle_effect_immunity_core(Machine &m) {
    computed::push(m, m.bx());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x5);
    lookup_table_byte_core(m);
    m.bx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x000B))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & uint8_t(m.cx())))));
    if (!(m.flags() & emu::ZF))
        goto branch_6943;
    return;
branch_6943:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleImmunityMode))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_694e;
    {
        const uint16_t callback_cs = m.cs();
        show_element_resistance_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_694e:;
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(kTextColorScratch), uint8_t(m.al()));
    return;
}
void check_battle_effect_immunity(Machine &m) {
    check_battle_effect_immunity_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x366955 (near)
// Return target defense in AL and the current effect's power in BL.
void read_target_defense_and_effect_power_core(Machine &m) {
    computed::push(m, m.bx());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x4);
    lookup_table_byte_core(m);
    m.bx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x8));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    return;
}
void read_target_defense_and_effect_power(Machine &m) {
    read_target_defense_and_effect_power_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x366973 (near)
// Compute target magic plus a randomized attacker-magic-scaled effect power.
void compute_magic_consumption_damage_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x000A))));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    battle_actor_pointer_core(m);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x000A))));
    computed::push(m, m.bx());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x4);
    lookup_table_byte_core(m);
    m.bx() = uint16_t(computed::pop(m));
    // P = power * floor(caster magic / 4); amount = P + caster magic + target magic + roll(0..low8(P)/2).
    m.set_ah(uint8_t(uint8_t(m.bx())));
    m.set_ah(uint8_t(computed::shift_right<uint8_t>(m, m.ah(), 0x2)));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.dx() = uint16_t(m.ax());
    m.set_ah(uint8_t(m.al()));
    m.set_ah(uint8_t(computed::shift_right<uint8_t>(m, m.ah(), 0x1)));
    {
        const uint16_t callback_cs = m.cs();
        add_random_and_stat_damage_bonus_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(uint8_t(m.cx())));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.ax()));
    m.wr16(m.ss(), uint16_t(kMeterInputTarget), uint16_t(m.dx()));
    return;
}
void compute_magic_consumption_damage(Machine &m) {
    compute_magic_consumption_damage_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x366A9A (near)
// Announce defense and play the selected shield's animation once per action.
void execute_defend_action_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_6aa5;
    goto branch_6b3f;
branch_6aa5:;
    {
        const uint16_t callback_cs = m.cs();
        announce_battle_defense_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_6ab3;
    goto branch_6b3f;
branch_6ab3:;
    selected_battle_equipment_call_core(m);
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4B);
    if (m.flags() & emu::ZF)
        goto branch_6ace;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4C);
    if (m.flags() & emu::ZF)
        goto branch_6ae3;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4D);
    if (m.flags() & emu::ZF)
        goto branch_6af8;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xE3);
    if (m.flags() & emu::ZF)
        goto branch_6b0d;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xF3);
    if (m.flags() & emu::ZF)
        goto branch_6b22;
    goto branch_6b3f;
branch_6ace:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    m.wr8(m.ss(), uint16_t(kActionTarget), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_shield_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6b3f;
branch_6ae3:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    m.wr8(m.ss(), uint16_t(kActionTarget), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_shield_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6b3f;
branch_6af8:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    m.wr8(m.ss(), uint16_t(kActionTarget), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_shield_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6b3f;
branch_6b0d:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    m.wr8(m.ss(), uint16_t(kActionTarget), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x4));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_shield_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6b3f;
branch_6b22:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    m.wr8(m.ss(), uint16_t(kActionTarget), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    m.wr8(m.ss(), uint16_t(kActionTarget), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x5));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_protective_aura_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6b3f;
branch_6b3f:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    return;
}
void execute_defend_action(Machine &m) {
    execute_defend_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x366B4A (near)
// Announce defense and barrier, animate the shield, and apply its resistance mask.
void execute_barrier_action_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_6b55;
    goto branch_6be8;
branch_6b55:;
    {
        const uint16_t callback_cs = m.cs();
        announce_battle_defense_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        show_barrier_formed_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_6b66;
    goto branch_6be8;
branch_6b66:;
    selected_battle_equipment_call_core(m);
    (void)computed::subtract<uint8_t>(m, m.al(), 0x13);
    if (m.flags() & emu::ZF)
        goto branch_6b7f;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4E);
    if (m.flags() & emu::ZF)
        goto branch_6b94;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4F);
    if (m.flags() & emu::ZF)
        goto branch_6ba9;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x50);
    if (m.flags() & emu::ZF)
        goto branch_6bbe;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x51);
    if (m.flags() & emu::ZF)
        goto branch_6bd3;
branch_6b7f:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    m.wr8(m.ss(), uint16_t(kActionTarget), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_shield_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6be8;
branch_6b94:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    m.wr8(m.ss(), uint16_t(kActionTarget), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x3));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_shield_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6be8;
branch_6ba9:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    m.wr8(m.ss(), uint16_t(kActionTarget), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_shield_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6be8;
branch_6bbe:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    m.wr8(m.ss(), uint16_t(kActionTarget), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x4));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_shield_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6be8;
branch_6bd3:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    m.wr8(m.ss(), uint16_t(kActionTarget), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x9));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_shield_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6be8;
branch_6be8:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x4);
    lookup_table_byte_core(m);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x000B))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | uint8_t(m.cx())))));
    m.wr8(m.ss(), uint16_t(m.si() + 0x000B), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    return;
}
void execute_barrier_action(Machine &m) {
    execute_barrier_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x366C12 (near)
// Announce the actor's counterattack stance once per action.
void execute_counter_stance_action_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_6c1d;
    {
        const uint16_t callback_cs = m.cs();
        show_counter_stance_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_6c1d:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    return;
}
void execute_counter_stance_action(Machine &m) {
    execute_counter_stance_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x366C28 (near)
// Select the weapon's hit animation and resolve its physical attack.
void execute_strength_weapon_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_6c37;
    goto branch_6e2e;
branch_6c37:;
    announce_battle_action_call_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_6c45;
    goto branch_6e2e;
branch_6c45:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_6c50;
    goto branch_6e2e;
branch_6c50:;
    selected_battle_equipment_call_core(m);
    (void)computed::subtract<uint8_t>(m, m.al(), 0x20);
    if (!(m.flags() & emu::ZF))
        goto branch_6c5c;
    goto branch_6cf6;
branch_6c5c:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x21);
    if (!(m.flags() & emu::ZF))
        goto branch_6c63;
    goto branch_6d04;
branch_6c63:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x22);
    if (!(m.flags() & emu::ZF))
        goto branch_6c6a;
    goto branch_6d12;
branch_6c6a:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x23);
    if (!(m.flags() & emu::ZF))
        goto branch_6c71;
    goto branch_6d20;
branch_6c71:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x24);
    if (!(m.flags() & emu::ZF))
        goto branch_6c78;
    goto branch_6d2e;
branch_6c78:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x25);
    if (!(m.flags() & emu::ZF))
        goto branch_6c7f;
    goto branch_6d3c;
branch_6c7f:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x35);
    if (!(m.flags() & emu::ZF))
        goto branch_6c86;
    goto branch_6d4a;
branch_6c86:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x36);
    if (!(m.flags() & emu::ZF))
        goto branch_6c8d;
    goto branch_6d58;
branch_6c8d:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x87);
    if (!(m.flags() & emu::ZF))
        goto branch_6c94;
    goto branch_6d66;
branch_6c94:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x88);
    if (!(m.flags() & emu::ZF))
        goto branch_6c9b;
    goto branch_6d74;
branch_6c9b:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x89);
    if (!(m.flags() & emu::ZF))
        goto branch_6ca2;
    goto branch_6d82;
branch_6ca2:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x8C);
    if (!(m.flags() & emu::ZF))
        goto branch_6ca9;
    goto branch_6d90;
branch_6ca9:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x8D);
    if (!(m.flags() & emu::ZF))
        goto branch_6cb0;
    goto branch_6d9e;
branch_6cb0:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x8E);
    if (!(m.flags() & emu::ZF))
        goto branch_6cb7;
    goto branch_6dac;
branch_6cb7:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x8F);
    if (!(m.flags() & emu::ZF))
        goto branch_6cbe;
    goto branch_6db9;
branch_6cbe:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x90);
    if (!(m.flags() & emu::ZF))
        goto branch_6cc5;
    goto branch_6dc6;
branch_6cc5:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x91);
    if (!(m.flags() & emu::ZF))
        goto branch_6ccc;
    goto branch_6dd3;
branch_6ccc:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x8B);
    if (!(m.flags() & emu::ZF))
        goto branch_6cd3;
    goto branch_6de0;
branch_6cd3:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xA7);
    if (!(m.flags() & emu::ZF))
        goto branch_6cda;
    goto branch_6dfa;
branch_6cda:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xB8);
    if (!(m.flags() & emu::ZF))
        goto branch_6ce1;
    goto branch_6e07;
branch_6ce1:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x95);
    if (!(m.flags() & emu::ZF))
        goto branch_6ce8;
    goto branch_6e14;
branch_6ce8:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x9D);
    if (!(m.flags() & emu::ZF))
        goto branch_6cef;
    goto branch_6ded;
branch_6cef:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xCC);
    if (!(m.flags() & emu::ZF))
        goto branch_6cf6;
    goto branch_6e21;
branch_6cf6:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_impact_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6e2e;
branch_6d04:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_hit_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6e2e;
branch_6d12:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x5));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_hit_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6e2e;
branch_6d20:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x2));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_hit_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6e2e;
branch_6d2e:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_hit_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6e2e;
branch_6d3c:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_hit_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6e2e;
branch_6d4a:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_hit_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6e2e;
branch_6d58:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_hit_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6e2e;
branch_6d66:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x3));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_impact_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6e2e;
branch_6d74:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_impact_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6e2e;
branch_6d82:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x2));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_battle_effect_4_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6e2e;
branch_6d90:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x4));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_battle_effect_5_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6e2e;
branch_6d9e:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_impact_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6e2e;
branch_6dac:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_impact_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6e2e;
branch_6db9:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_hit_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6e2e;
branch_6dc6:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x3));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_impact_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6e2e;
branch_6dd3:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x3));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_impact_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6e2e;
branch_6de0:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_impact_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6e2e;
branch_6ded:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x4));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_battle_effect_5_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6e2e;
branch_6dfa:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_impact_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6e2e;
branch_6e07:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_impact_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6e2e;
branch_6e14:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x3));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_battle_effect_3_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6e2e;
branch_6e21:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x3));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_hit_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_6e2e;
branch_6e2e:;
    resolve_physical_attack_call_core(m);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_strength_weapon_action(Machine &m) {
    execute_strength_weapon_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x3671BE (near)
// Resolve an element-sensitive weapon hit with a chance to instantly defeat a vulnerable target.
void execute_element_slayer_weapon_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_7237;
    announce_battle_action_call_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_7237;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_7237;
    selected_battle_equipment_call_core(m);
    (void)computed::subtract<uint8_t>(m, m.al(), 0x11);
    if (m.flags() & emu::ZF)
        goto branch_71f6;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2A);
    if (m.flags() & emu::ZF)
        goto branch_7203;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2B);
    if (m.flags() & emu::ZF)
        goto branch_7210;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2C);
    if (m.flags() & emu::ZF)
        goto branch_721d;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xE5);
    if (m.flags() & emu::ZF)
        goto branch_722a;
branch_71f6:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x5));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_hit_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_7237;
branch_7203:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x3));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_hit_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_7237;
branch_7210:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x2));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_hit_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_7237;
branch_721d:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_hit_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_7237;
branch_722a:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x2));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_impact_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_7237;
branch_7237:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x000C))));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x5);
    lookup_table_byte_core(m);
    m.cx() =
        uint16_t((m.cx() & 0xFF00) | uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.cx()) & m.al()))));
    if (!(m.flags() & emu::ZF))
        goto branch_7258;
    resolve_physical_attack_call_core(m);
    goto branch_730a;
    // A matching vulnerability/species mask gives an instant kill when a 0..100 roll is below 31.
branch_7258:;
    m.set_al(uint8_t(0x0));
    m.set_ah(uint8_t(0x64));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1F);
    if (!(m.flags() & emu::CF))
        goto branch_72c0;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0002))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | 0x1))));
    if (!BattleActorState(m, m.si()).write_status(uint8_t(m.al()))) {
        show_no_effect_message_call_core(m);
        goto branch_730a;
    }
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.si() + 0x0003)));
    m.ax() = uint16_t(0x0);
    BattleActorState(m, m.si()).write_hp(uint16_t(m.ax()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_728e;
    remove_defeated_enemy_group_core(m);
    goto branch_7291;
branch_728e:;
    shake_screen_for_party_hit_core(m);
branch_7291:;
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0xF));
    draw_battle_skill_list_core(m);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    draw_battle_member_panels_core(m);
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t callback_cs = m.cs();
        show_critical_hit_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    show_target_died_message_call_core(m);
    goto branch_730a;
branch_72c0:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    battle_actor_pointer_core(m);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0007))));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0002))));
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x4));
    if (m.flags() & emu::ZF)
        goto branch_72d5;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::shift_right<uint8_t>(m, uint8_t(m.cx()), 0x1)));
    // Fallback damage = power * floor(effective strength / 4) + effective strength +
    // roll(0..low8(product)/2).
branch_72d5:;
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(uint8_t(m.cx())));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::shift_right<uint8_t>(m, uint8_t(m.bx()), 0x2)));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    computed::push(m, m.bx());
    m.bx() = uint16_t(0x4);
    lookup_table_byte_core(m);
    m.bx() = uint16_t(computed::pop(m));
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    {
        const uint16_t product = uint16_t(m.al()) * uint8_t(m.bx());
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.dx() = uint16_t(m.ax());
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x0)) << 8));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.cx()));
    m.set_ah(uint8_t(m.al()));
    m.set_ah(uint8_t(computed::shift_right<uint8_t>(m, m.ah(), 0x1)));
    m.set_al(uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.set_ah(uint8_t(0x0));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.ax()));
    set_battle_meter_target_core(m);
    open_party_member_popup_core(m);
    check_target_knocked_out_core(m);
branch_730a:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_element_slayer_weapon_action(Machine &m) {
    execute_element_slayer_weapon_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x367319 (near)
// Resolve a species-sensitive weapon hit with a chance to instantly defeat a vulnerable target.
void execute_species_slayer_weapon_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_7381;
    announce_battle_action_call_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_7381;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_7381;
    selected_battle_equipment_call_core(m);
    (void)computed::subtract<uint8_t>(m, m.al(), 0x26);
    if (m.flags() & emu::ZF)
        goto branch_734d;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x27);
    if (m.flags() & emu::ZF)
        goto branch_735a;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x28);
    if (m.flags() & emu::ZF)
        goto branch_7367;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x29);
    if (m.flags() & emu::ZF)
        goto branch_7374;
branch_734d:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x2));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_hit_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_7381;
branch_735a:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x4));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_hit_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_7381;
branch_7367:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x3));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_hit_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_7381;
branch_7374:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_hit_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_7381;
branch_7381:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0001))));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x5);
    lookup_table_byte_core(m);
    m.cx() =
        uint16_t((m.cx() & 0xFF00) | uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.cx()) & m.al()))));
    if (!(m.flags() & emu::ZF))
        goto branch_73a2;
    resolve_physical_attack_call_core(m);
    goto branch_7454;
    // A matching vulnerability/species mask gives an instant kill when a 0..100 roll is below 31.
branch_73a2:;
    m.set_al(uint8_t(0x0));
    m.set_ah(uint8_t(0x64));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1F);
    if (!(m.flags() & emu::CF))
        goto branch_740a;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0002))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | 0x1))));
    if (!BattleActorState(m, m.si()).write_status(uint8_t(m.al()))) {
        show_no_effect_message_call_core(m);
        goto branch_7454;
    }
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.si() + 0x0003)));
    m.ax() = uint16_t(0x0);
    BattleActorState(m, m.si()).write_hp(uint16_t(m.ax()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_73d8;
    remove_defeated_enemy_group_core(m);
    goto branch_73db;
branch_73d8:;
    shake_screen_for_party_hit_core(m);
branch_73db:;
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0xF));
    draw_battle_skill_list_core(m);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    draw_battle_member_panels_core(m);
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t callback_cs = m.cs();
        show_critical_hit_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    show_target_died_message_call_core(m);
    goto branch_7454;
branch_740a:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    battle_actor_pointer_core(m);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0007))));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0002))));
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x4));
    if (m.flags() & emu::ZF)
        goto branch_741f;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::shift_right<uint8_t>(m, uint8_t(m.cx()), 0x1)));
    // Fallback damage = power * floor(effective strength / 4) + effective strength +
    // roll(0..low8(product)/2).
branch_741f:;
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(uint8_t(m.cx())));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::shift_right<uint8_t>(m, uint8_t(m.bx()), 0x2)));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    computed::push(m, m.bx());
    m.bx() = uint16_t(0x4);
    lookup_table_byte_core(m);
    m.bx() = uint16_t(computed::pop(m));
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    {
        const uint16_t product = uint16_t(m.al()) * uint8_t(m.bx());
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.dx() = uint16_t(m.ax());
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x0)) << 8));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.cx()));
    m.set_ah(uint8_t(m.al()));
    m.set_ah(uint8_t(computed::shift_right<uint8_t>(m, m.ah(), 0x1)));
    m.set_al(uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.set_ah(uint8_t(0x0));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.ax()));
    set_battle_meter_target_core(m);
    open_party_member_popup_core(m);
    check_target_knocked_out_core(m);
branch_7454:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_species_slayer_weapon_action(Machine &m) {
    execute_species_slayer_weapon_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x367463 (near)
// Damage the target with a magic-scaled drain hit and restore the attacker's HP.
void execute_hp_drain_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_74d7;
    announce_battle_action_call_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_74d7;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_74d7;
    selected_battle_equipment_call_core(m);
    (void)computed::subtract<uint8_t>(m, m.al(), 0x34);
    if (m.flags() & emu::ZF)
        goto branch_7493;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xB6);
    if (m.flags() & emu::ZF)
        goto branch_74b1;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xB7);
    if (m.flags() & emu::ZF)
        goto branch_74c4;
branch_7493:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x8));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_hit_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x3));
    m.set_al(uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectTargetMode), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_life_drain_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_74d7;
branch_74b1:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    m.set_al(uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectTargetMode), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_life_drain_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_74d7;
branch_74c4:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x3));
    m.set_al(uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectTargetMode), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_life_drain_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_74d7;
branch_74d7:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0001))));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x5);
    lookup_table_byte_core(m);
    m.cx() =
        uint16_t((m.cx() & 0xFF00) | uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.cx()) & m.al()))));
    if (m.flags() & emu::ZF)
        goto branch_74f8;
    show_no_effect_message_call_core(m);
    goto branch_75ce;
branch_74f8:;
    {
        const uint16_t callback_cs = m.cs();
        test_drain_hit_by_agility_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    (void)computed::subtract<uint8_t>(m, m.al(), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_7504;
    goto branch_75ce;
    // P = power * floor(caster magic / 4); drain = P + magic + floor(roll(0..low8(P)) / 2).
branch_7504:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x000A))));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x0)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(uint8_t(m.cx())));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::shift_right<uint8_t>(m, uint8_t(m.bx()), 0x2)));
    computed::push(m, m.bx());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x4);
    lookup_table_byte_core(m);
    m.bx() = uint16_t(computed::pop(m));
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    {
        const uint16_t product = uint16_t(m.al()) * uint8_t(m.bx());
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.dx() = uint16_t(m.ax());
    computed::push(m, m.dx());
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.cx()));
    m.ax() = uint16_t(computed::pop(m));
    m.set_ah(uint8_t(m.al()));
    m.set_al(uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.set_ah(uint8_t(0x0));
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x1));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.ax()));
    set_battle_meter_target_core(m);
    {
        const uint16_t callback_cs = m.cs();
        show_hp_drained_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_7576;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    m.wr8(m.ss(), uint16_t(kTextColorScratch), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    m.wr8(m.ss(), uint16_t(kActionTarget), uint8_t(m.al()));
    m.set_al(uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectTargetMode), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_healing_glow_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kTextColorScratch))));
    m.wr8(m.ss(), uint16_t(kActionTarget), uint8_t(m.al()));
    // Heal the caster by DX, capped at max HP; the original uses the post-meter DX value.
branch_7576:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    battle_actor_pointer_core(m);
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.si() + 0x0003)));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.bx() = uint16_t(m.rd16(m.ss(), uint16_t(m.si() + 0x0005)));
    (void)computed::subtract<uint16_t>(m, m.ax(), m.bx());
    if (m.flags() & emu::CF)
        goto branch_758f;
    if (m.flags() & emu::ZF)
        goto branch_758f;
    m.ax() = uint16_t(m.bx());
branch_758f:;
    BattleActorState(m, m.si()).write_hp(uint16_t(m.ax()));
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0xF));
    draw_battle_skill_list_core(m);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    check_target_knocked_out_core(m);
    if (m.flags() & emu::CF)
        goto branch_75ce;
    has_table_flag_bit1_core(m);
    if (m.flags() & emu::CF)
        goto branch_75ac;
    goto branch_75ce;
branch_75ac:;
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x2));
    if (!(m.flags() & emu::ZF))
        goto branch_75ba;
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x4));
    if (!(m.flags() & emu::ZF))
        goto branch_75c4;
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x10));
    if (!(m.flags() & emu::ZF))
        goto branch_75c9;
    goto branch_75ce;
branch_75ba:;
    resolve_drain_hit_call_core(m);
    goto branch_75ce;
branch_75c4:;
    resolve_status_hit_call_core(m);
    goto branch_75ce;
branch_75c9:;
    resolve_fixed_damage_hit_call_core(m);
    goto branch_75ce;
branch_75ce:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_hp_drain_action(Machine &m) {
    execute_hp_drain_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x3675DD (near)
// Restore target HP with randomized caster-magic-scaled healing and show recovery.
void execute_magic_healing_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_75ec;
    goto branch_7680;
branch_75ec:;
    {
        const uint16_t callback_cs = m.cs();
        announce_used_battle_item_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_75fa;
    goto branch_7680;
branch_75fa:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_7680;
    selected_battle_equipment_call_core(m);
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_7619;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_766d;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x81);
    if (m.flags() & emu::ZF)
        goto branch_7637;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x82);
    if (m.flags() & emu::ZF)
        goto branch_7655;
    goto branch_7680;
branch_7619:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_spell_casting_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectTargetMode), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x2));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_healing_glow_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_7680;
branch_7637:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_spell_casting_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x2));
    m.set_al(uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectTargetMode), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_healing_glow_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_7680;
branch_7655:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_spell_casting_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x2));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_healing_glow_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_7680;
branch_766d:;
    m.set_al(uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectTargetMode), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x2));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_healing_glow_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_7680;
    // Healing uses power * floor(caster magic / 4) + caster magic + target magic + roll(0..low8(product)/2).
branch_7680:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x000A))));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    battle_actor_pointer_core(m);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x000A))));
    computed::push(m, m.bx());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x4);
    lookup_table_byte_core(m);
    m.bx() = uint16_t(computed::pop(m));
    m.set_ah(uint8_t(uint8_t(m.bx())));
    m.set_ah(uint8_t(computed::shift_right<uint8_t>(m, m.ah(), 0x2)));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.dx() = uint16_t(m.ax());
    m.set_ah(uint8_t(m.al()));
    m.set_ah(uint8_t(computed::shift_right<uint8_t>(m, m.ah(), 0x1)));
    {
        const uint16_t callback_cs = m.cs();
        add_random_and_stat_damage_bonus_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(uint8_t(m.cx())));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.ax()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.si() + 0x0003)));
    m.cx() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.bx() = uint16_t(m.rd16(m.ss(), uint16_t(m.si() + 0x0005)));
    (void)computed::subtract<uint16_t>(m, m.ax(), m.bx());
    if (m.flags() & emu::CF)
        goto branch_76d3;
    if (m.flags() & emu::ZF)
        goto branch_76d3;
    m.ax() = uint16_t(m.bx());
    // Clamp HP to maximum and report only the actual recovered amount.
branch_76d3:;
    BattleActorState(m, m.si()).write_hp(uint16_t(m.ax()));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.cx()));
    m.wr16(m.ss(), uint16_t(kMeterInputTarget), uint16_t(m.ax()));
    {
        const uint16_t callback_cs = m.cs();
        show_target_hp_recovery_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0xF));
    draw_battle_skill_list_core(m);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_magic_healing_action(Machine &m) {
    execute_magic_healing_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x3676FC (near)
// Resolve a randomized agility-scaled weapon hit after target defense and resistance.
void execute_agility_weapon_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_770b;
    goto branch_779f;
branch_770b:;
    announce_battle_action_call_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_7719;
    goto branch_779f;
branch_7719:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_779f;
    selected_battle_equipment_call_core(m);
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2D);
    if (m.flags() & emu::ZF)
        goto branch_7744;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2E);
    if (m.flags() & emu::ZF)
        goto branch_7751;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2F);
    if (m.flags() & emu::ZF)
        goto branch_775e;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xAC);
    if (m.flags() & emu::ZF)
        goto branch_776b;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x8A);
    if (m.flags() & emu::ZF)
        goto branch_7778;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x30);
    if (m.flags() & emu::ZF)
        goto branch_7785;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xC4);
    if (m.flags() & emu::ZF)
        goto branch_7792;
    goto branch_779f;
branch_7744:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_hit_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_779f;
branch_7751:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_hit_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_779f;
branch_775e:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x3));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_lightsaber_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_779f;
branch_776b:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_impact_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_779f;
branch_7778:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_impact_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_779f;
branch_7785:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_battle_effect_3_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_779f;
branch_7792:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_impact_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_779f;
branch_779f:;
    {
        const uint16_t callback_cs = m.cs();
        read_target_effective_defense_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    battle_actor_pointer_core(m);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0009))));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0002))));
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x8));
    if (m.flags() & emu::ZF)
        goto branch_77b7;
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::shift_right<uint8_t>(m, uint8_t(m.bx()), 0x1)));
    // P = power * floor(effective agility / 4); damage = P + agility + roll(0..low8(P)/2) - effective
    // defense.
branch_77b7:;
    computed::push(m, m.bx());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x4);
    lookup_table_byte_core(m);
    m.bx() = uint16_t(computed::pop(m));
    m.set_ah(uint8_t(uint8_t(m.bx())));
    m.set_ah(uint8_t(computed::shift_right<uint8_t>(m, m.ah(), 0x2)));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.dx() = uint16_t(m.ax());
    m.set_ah(uint8_t(m.al()));
    m.set_ah(uint8_t(computed::shift_right<uint8_t>(m, m.ah(), 0x1)));
    {
        const uint16_t callback_cs = m.cs();
        add_random_and_stat_damage_bonus_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(uint8_t(m.cx())));
    m.dx() = uint16_t(computed::subtract<uint16_t>(m, m.dx(), m.ax()));
    has_table_flag_bit1_core(m);
    if (!(m.flags() & emu::CF))
        goto branch_77f7;
    if (m.flags() & emu::ZF)
        goto branch_77e4;
    m.dx() = uint16_t(computed::shift_right<uint16_t>(m, m.dx(), 0x1, true));
branch_77e4:;
    computed::push(m, m.dx());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x000B))));
    m.dx() = uint16_t(computed::pop(m));
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x40));
    if (m.flags() & emu::ZF)
        goto branch_77f7;
    m.dx() = uint16_t(computed::shift_right<uint16_t>(m, m.dx(), 0x1, true));
branch_77f7:;
    m.wr16(m.ss(), uint16_t(kMeterInputTarget), uint16_t(m.dx()));
    (void)computed::subtract<uint16_t>(m, m.dx(), 0x1);
    if (bool(m.flags() & emu::SF) == bool(m.flags() & emu::OF))
        goto branch_780e;
    show_no_damage_message_call_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionEffect))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_7833;
    goto branch_7842;
branch_780e:;
    set_battle_meter_target_core(m);
    open_party_member_popup_core(m);
    check_target_knocked_out_core(m);
    if (m.flags() & emu::CF)
        goto branch_7842;
    has_table_flag_bit1_core(m);
    if (m.flags() & emu::CF)
        goto branch_7820;
    goto branch_7842;
branch_7820:;
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x2));
    if (!(m.flags() & emu::ZF))
        goto branch_782e;
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x4));
    if (!(m.flags() & emu::ZF))
        goto branch_7838;
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x10));
    if (!(m.flags() & emu::ZF))
        goto branch_783d;
    goto branch_7842;
branch_782e:;
    resolve_drain_hit_call_core(m);
    goto branch_7842;
branch_7833:;
    resolve_counterattack_core(m);
    goto branch_7842;
branch_7838:;
    resolve_status_hit_call_core(m);
    goto branch_7842;
branch_783d:;
    resolve_fixed_damage_hit_call_core(m);
    goto branch_7842;
branch_7842:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_agility_weapon_action(Machine &m) {
    execute_agility_weapon_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x367851 (near)
// Apply a magic-scaled technique minus raw target defense, with resistance and follow-up reactions.
void execute_strength_technique_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_7899;
    announce_battle_action_call_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_7899;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_7899;
    selected_battle_equipment_call_core(m);
    (void)computed::subtract<uint8_t>(m, m.al(), 0x31);
    if (m.flags() & emu::ZF)
        goto branch_787f;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x32);
    if (m.flags() & emu::ZF)
        goto branch_788c;
    goto branch_7899;
branch_787f:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x2));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_hit_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_7899;
branch_788c:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_hit_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_7899;
    // Original quirk: this 'strength' technique reads caster magic (field 0Ah), then subtracts raw target
    // defense.
branch_7899:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0008))));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    battle_actor_pointer_core(m);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x000A))));
    computed::push(m, m.bx());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x4);
    lookup_table_byte_core(m);
    m.bx() = uint16_t(computed::pop(m));
    // Damage = power * floor(magic / 4) + magic + roll(0..low8(product)/2) - defense, before resistance.
    m.set_ah(uint8_t(uint8_t(m.bx())));
    m.set_ah(uint8_t(computed::shift_right<uint8_t>(m, m.ah(), 0x2)));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.dx() = uint16_t(m.ax());
    m.set_ah(uint8_t(m.al()));
    m.set_ah(uint8_t(computed::shift_right<uint8_t>(m, m.ah(), 0x1)));
    {
        const uint16_t callback_cs = m.cs();
        add_random_and_stat_damage_bonus_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(uint8_t(m.cx())));
    m.dx() = uint16_t(computed::subtract<uint16_t>(m, m.dx(), m.ax()));
    {
        const uint16_t callback_cs = m.cs();
        halve_damage_for_effect_resistance_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    (void)computed::subtract<uint16_t>(m, m.dx(), 0x1);
    if (bool(m.flags() & emu::SF) == bool(m.flags() & emu::OF))
        goto branch_78e6;
    show_no_damage_message_call_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionEffect))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_790b;
    goto branch_791a;
branch_78e6:;
    set_battle_meter_target_core(m);
    open_party_member_popup_core(m);
    check_target_knocked_out_core(m);
    if (m.flags() & emu::CF)
        goto branch_791a;
    has_table_flag_bit1_core(m);
    if (m.flags() & emu::CF)
        goto branch_78f8;
    goto branch_791a;
branch_78f8:;
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x2));
    if (!(m.flags() & emu::ZF))
        goto branch_7906;
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x4));
    if (!(m.flags() & emu::ZF))
        goto branch_7910;
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x10));
    if (!(m.flags() & emu::ZF))
        goto branch_7915;
    goto branch_791a;
branch_7906:;
    resolve_drain_hit_call_core(m);
    goto branch_791a;
branch_790b:;
    resolve_counterattack_core(m);
    goto branch_791a;
branch_7910:;
    resolve_status_hit_call_core(m);
    goto branch_791a;
branch_7915:;
    resolve_fixed_damage_hit_call_core(m);
    goto branch_791a;
branch_791a:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_strength_technique_action(Machine &m) {
    execute_strength_technique_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x367929 (near)
// Add strength and item power to damage, then announce the weapon shattering.
void execute_shattering_weapon_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_7955;
    announce_battle_action_call_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_7955;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_7955;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_glass_sword_shatter_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_7955;
    // Damage = raw attacker strength + power + roll(0..field 5), without subtracting defense.
branch_7955:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x4);
    lookup_table_byte_core(m);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x5);
    lookup_table_byte_core(m);
    m.set_ah(uint8_t(m.al()));
    m.set_al(uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0007))));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.al()));
    m.dx() = uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(0x0)) << 8));
    m.set_ah(uint8_t(0x0));
    m.set_al(uint8_t(uint8_t(m.bx())));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.ax()));
    m.set_al(uint8_t(uint8_t(m.cx())));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.ax()));
    set_battle_meter_target_core(m);
    open_party_member_popup_core(m);
    {
        const uint16_t callback_cs = m.cs();
        show_weapon_shattered_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    check_target_knocked_out_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_shattering_weapon_action(Machine &m) {
    execute_shattering_weapon_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x3679AE (near)
// Compute revenge damage using target strength, item power and random spread.
void execute_revenge_weapon_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_7a25;
    announce_battle_action_call_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_79d8;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_79d8;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_revenge_sword_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    // Original quirk: CL saves the prior AL, not a freshly read defense byte.
branch_79d8:;
    m.set_ah(uint8_t(0x0));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0007))));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0002))));
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x4));
    if (m.flags() & emu::ZF)
        goto branch_79f3;
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(computed::shift_right<uint8_t>(m, uint8_t(m.dx()), 0x1)));
    // Start with effective target strength + power + roll(0..power); the reduction helper subtracts saved CL.
branch_79f3:;
    m.dx() = uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(0x0)) << 8));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x4);
    lookup_table_byte_core(m);
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.ax()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x4);
    lookup_table_byte_core(m);
    m.set_ah(uint8_t(m.al()));
    {
        const uint16_t callback_cs = m.cs();
        add_random_damage_bonus_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.wr16(m.ss(), uint16_t(kMeterInputTarget), uint16_t(m.dx()));
    {
        const uint16_t callback_cs = m.cs();
        reduce_damage_by_defense_and_guard_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.wr16(m.ss(), uint16_t(kMeterInputTarget), uint16_t(m.dx()));
branch_7a25:;
    m.dx() = uint16_t(m.rd16(m.ss(), uint16_t(kMeterInputTarget)));
    m.dx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.dx() | m.dx())));
    if (!(m.flags() & emu::ZF))
        goto branch_7a3d;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
branch_7a3d:;
    set_battle_meter_target_core(m);
    open_party_member_popup_core(m);
    check_target_knocked_out_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_revenge_weapon_action(Machine &m) {
    execute_revenge_weapon_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x367A55 (near)
// Apply strength plus randomized item power without a defense subtraction.
void execute_strength_ignoring_defense_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_7a81;
    announce_battle_action_call_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_7a81;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_7a81;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x7));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_decisive_slash_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_7a81;
    // Damage = effective strength + byte(power + roll(0..field 5)); the power/spread sum wraps at 256.
branch_7a81:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x4);
    lookup_table_byte_core(m);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x5);
    lookup_table_byte_core(m);
    m.set_ah(uint8_t(m.al()));
    m.set_al(uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::add<uint8_t>(m, uint8_t(m.cx()), m.al())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    battle_actor_pointer_core(m);
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0007))));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0002))));
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x4));
    if (m.flags() & emu::ZF)
        goto branch_7abb;
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(computed::shift_right<uint8_t>(m, uint8_t(m.dx()), 0x1)));
branch_7abb:;
    m.dx() = uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(0x0)) << 8));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(uint8_t(m.cx())));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.ax()));
    set_battle_meter_target_core(m);
    open_party_member_popup_core(m);
    check_target_knocked_out_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_strength_ignoring_defense_action(Machine &m) {
    execute_strength_ignoring_defense_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x367ADB (near)
// Use the chainsaw's accuracy test and defense comparison to dismember its target.
void execute_chainsaw_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_7b23;
    announce_battle_action_call_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_7b23;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_7b23;
    selected_battle_equipment_call_core(m);
    (void)computed::subtract<uint8_t>(m, m.al(), 0x92);
    if (m.flags() & emu::ZF)
        goto branch_7b16;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x42);
    if (m.flags() & emu::ZF)
        goto branch_7b09;
    goto branch_7b23;
branch_7b09:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_chainsaw_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_7b23;
branch_7b16:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_chainsaw_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_7b23;
branch_7b23:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0007))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kBattleItemPower), uint8_t(uint8_t(m.bx())));
    computed::push(m, m.bx());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x5);
    lookup_table_byte_core(m);
    m.bx() = uint16_t(computed::pop(m));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), uint8_t(m.bx()))));
    if (!(m.flags() & emu::CF))
        goto branch_7b49;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    // Miss when a 0..100 roll is below max(field 5 - raw strength, 0).
branch_7b49:;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0x6400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.cx()));
    if (!(m.flags() & emu::CF))
        goto branch_7b5d;
    show_attack_missed_message_call_core(m);
    goto branch_7bfe;
branch_7b5d:;
    {
        const uint16_t callback_cs = m.cs();
        read_target_effective_defense_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.wr8(m.ss(), uint16_t(kBattleSavedAttackValue), uint8_t(uint8_t(m.cx())));
    computed::push(m, m.bx());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x4);
    lookup_table_byte_core(m);
    m.bx() = uint16_t(computed::pop(m));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0002))));
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x40));
    if (m.flags() & emu::ZF)
        goto branch_7b90;
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kBattleItemPower))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::shift_right<uint8_t>(m, uint8_t(m.bx()), 0x1)));
    m.wr8(m.ss(), uint16_t(kBattleItemPower), uint8_t(uint8_t(m.bx())));
branch_7b90:;
    has_table_flag_bit1_core(m);
    if (!(m.flags() & emu::CF))
        goto branch_7ba3;
    if (m.flags() & emu::ZF)
        goto branch_7ba3;
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kBattleItemPower))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::shift_right<uint8_t>(m, uint8_t(m.bx()), 0x1)));
    m.wr8(m.ss(), uint16_t(kBattleItemPower), uint8_t(uint8_t(m.bx())));
    // Original quirk: dismember when adjusted strength < defense, retaining the reversed comparison.
branch_7ba3:;
    m.set_al(uint8_t(uint8_t(m.bx())));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kBattleSavedAttackValue))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), uint8_t(m.cx()))));
    if (!(m.flags() & emu::CF))
        goto branch_7bd2;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0002))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | 0x1))));
    if (!BattleActorState(m, m.si()).write_status(uint8_t(m.al()))) {
        show_no_effect_message_call_core(m);
        goto branch_7bfe;
    }
    {
        const uint16_t callback_cs = m.cs();
        show_target_dismembered_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.si() + 0x0003)));
    m.ax() = uint16_t(0x0);
    BattleActorState(m, m.si()).write_hp(uint16_t(m.ax()));
    check_target_knocked_out_core(m);
    goto branch_7bfe;
branch_7bd2:;
    show_no_damage_message_call_core(m);
    has_table_flag_bit1_core(m);
    if (m.flags() & emu::CF)
        goto branch_7be1;
    goto branch_7bfe;
branch_7be1:;
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x2));
    if (!(m.flags() & emu::ZF))
        goto branch_7bef;
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x4));
    if (!(m.flags() & emu::ZF))
        goto branch_7bf4;
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x10));
    if (!(m.flags() & emu::ZF))
        goto branch_7bf9;
    goto branch_7bfe;
branch_7bef:;
    resolve_counterattack_core(m);
    goto branch_7bfe;
branch_7bf4:;
    resolve_status_hit_call_core(m);
    goto branch_7bfe;
branch_7bf9:;
    resolve_fixed_damage_hit_call_core(m);
    goto branch_7bfe;
branch_7bfe:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_chainsaw_action(Machine &m) {
    execute_chainsaw_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x367C0D (near)
// Check firearm accuracy and apply randomized base damage against target defense.
void execute_firearm_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_7c80;
    announce_battle_action_call_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_7c80;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_7c80;
    selected_battle_equipment_call_core(m);
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3D);
    if (m.flags() & emu::ZF)
        goto branch_7c47;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3E);
    if (m.flags() & emu::ZF)
        goto branch_7c54;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3F);
    if (m.flags() & emu::ZF)
        goto branch_7c61;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1B);
    if (m.flags() & emu::ZF)
        goto branch_7c6e;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xE7);
    if (m.flags() & emu::ZF)
        goto branch_7c80;
    goto branch_7c80;
branch_7c47:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x4));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_firearm_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_7c80;
branch_7c54:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x3));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_firearm_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_7c80;
branch_7c61:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x4));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_firearm_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_7c80;
branch_7c6e:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_thrown_rock_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_rock_impact_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_7c80;
branch_7c80:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    battle_actor_pointer_core(m);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0007))));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0002))));
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x4));
    if (m.flags() & emu::ZF)
        goto branch_7c95;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::shift_left<uint8_t>(m, uint8_t(m.cx()), 0x1)));
    // Original quirk: the preceding weakened-strength doubling in CL is unused; accuracy reloads raw
    // strength.
branch_7c95:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x5);
    lookup_table_byte_core(m);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0007))));
    m.dx() = uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.set_al(uint8_t(uint8_t(m.bx())));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), uint8_t(m.dx() >> 8))));
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x80));
    if (m.flags() & emu::ZF)
        goto branch_7cb3;
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() ^ m.al()))));
    // Miss threshold is byte(field 5 - raw strength), replaced by zero if its sign bit is set.
branch_7cb3:;
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.set_ah(uint8_t(0x64));
    m.set_al(uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.bx()));
    if (!(m.flags() & emu::CF))
        goto branch_7cc7;
    show_attack_missed_message_call_core(m);
    goto branch_7ccd;
branch_7cc7:;
    {
        const uint16_t callback_cs = m.cs();
        read_target_effective_defense_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        apply_base_effect_damage_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_7ccd:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_firearm_action(Machine &m) {
    execute_firearm_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x367CDC (near)
// Check bow accuracy against attacker agility and apply base effect damage.
void execute_bow_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_7d35;
    announce_battle_action_call_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_7d35;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_7d35;
    selected_battle_equipment_call_core(m);
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3A);
    if (m.flags() & emu::ZF)
        goto branch_7d0e;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3B);
    if (m.flags() & emu::ZF)
        goto branch_7d1b;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3C);
    if (m.flags() & emu::ZF)
        goto branch_7d28;
    goto branch_7d35;
branch_7d0e:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_bow_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_7d35;
branch_7d1b:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_bow_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_7d35;
branch_7d28:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x3));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_bow_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_7d35;
branch_7d35:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    battle_actor_pointer_core(m);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0009))));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0002))));
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x8));
    if (m.flags() & emu::ZF)
        goto branch_7d4a;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::shift_right<uint8_t>(m, uint8_t(m.cx()), 0x1)));
branch_7d4a:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x5);
    lookup_table_byte_core(m);
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), uint8_t(m.cx()))));
    if (!(m.flags() & emu::CF))
        goto branch_7d5c;
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() ^ m.al()))));
    // Miss when a 0..100 roll is below max(field 5 - effective attacker agility, 0).
branch_7d5c:;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0x6400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.cx()));
    if (!(m.flags() & emu::CF))
        goto branch_7d6f;
    show_attack_missed_message_call_core(m);
    goto branch_7d75;
branch_7d6f:;
    {
        const uint16_t callback_cs = m.cs();
        read_target_effective_defense_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        apply_base_effect_damage_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_7d75:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_bow_action(Machine &m) {
    execute_bow_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x367D84 (near)
// Check agility-based accuracy, cancel the target's pending action and apply whip damage.
void execute_whip_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_7dd7;
    announce_battle_action_call_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_7dd7;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_7dd7;
    selected_battle_equipment_call_core(m);
    (void)computed::subtract<uint8_t>(m, m.al(), 0x40);
    if (m.flags() & emu::ZF)
        goto branch_7db2;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x41);
    if (m.flags() & emu::ZF)
        goto branch_7dbf;
    goto branch_7dd7;
branch_7db2:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_whip_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_7dd7;
branch_7dbf:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_whip_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_battle_effect_6_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_7dd7;
    // Hit when a 0..100 roll is below byte(field 5 + attacker agility - target agility); subtraction wraps.
branch_7dd7:;
    {
        const uint16_t callback_cs = m.cs();
        read_effective_battle_agilities_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    computed::push(m, m.bx());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x5);
    lookup_table_byte_core(m);
    m.bx() = uint16_t(computed::pop(m));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), uint8_t(m.cx()))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), uint8_t(m.bx()))));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(0x0));
    m.set_ah(uint8_t(0x64));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.cx()));
    if (m.flags() & emu::CF)
        goto branch_7e00;
    show_attack_missed_message_call_core(m);
    goto branch_7e2c;
branch_7e00:;
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1F));
    m.si() = uint16_t(kBattleOrder);
branch_7e0a:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0003))));
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.dx()));
    if (m.flags() & emu::ZF)
        goto branch_7e1e;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x4));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    if (!(m.flags() & emu::ZF))
        goto branch_7e0a;
    show_attack_missed_message_call_core(m);
    goto branch_7e2c;
branch_7e1e:;
    m.set_al(uint8_t(0xFF));
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    {
        const uint16_t callback_cs = m.cs();
        show_whip_entangled_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        read_target_effective_defense_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        apply_base_effect_damage_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_7e2c:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_whip_action(Machine &m) {
    execute_whip_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x367E3B (near)
// Animate heavy ordnance and apply its power and spread after target defense.
void execute_heavy_ordnance_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_7ec0;
    announce_battle_action_call_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_7ec0;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_7ec0;
    selected_battle_equipment_call_core(m);
    (void)computed::subtract<uint8_t>(m, m.al(), 0x43);
    if (m.flags() & emu::ZF)
        goto branch_7e71;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x44);
    if (m.flags() & emu::ZF)
        goto branch_7e7e;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x45);
    if (m.flags() & emu::ZF)
        goto branch_7e91;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x46);
    if (m.flags() & emu::ZF)
        goto branch_7eb3;
    goto branch_7ec0;
branch_7e71:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_automatic_gun_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_7ec0;
branch_7e7e:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    m.set_al(uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectTargetMode), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_explosive_blast_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_7ec0;
branch_7e91:;
    m.set_al(uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectTargetMode), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x3));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_missile_projectile_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_missile_trails_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_explosive_blast_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_explosive_blast_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_7ec0;
branch_7eb3:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x3));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_automatic_gun_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_7ec0;
branch_7ec0:;
    {
        const uint16_t callback_cs = m.cs();
        read_target_effective_defense_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        roll_effect_power_and_spread_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        reduce_damage_by_defense_and_guard_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    (void)computed::subtract<uint16_t>(m, m.dx(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_7ed9;
    set_battle_meter_target_core(m);
    open_party_member_popup_core(m);
    check_target_knocked_out_core(m);
    goto branch_7edc;
branch_7ed9:;
    show_no_damage_message_call_core(m);
branch_7edc:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_heavy_ordnance_action(Machine &m) {
    execute_heavy_ordnance_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x367EEB (near)
// Check agility-based beam accuracy and apply base effect damage.
void execute_beam_weapon_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_7f33;
    announce_battle_action_call_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_7f33;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_7f33;
    selected_battle_equipment_call_core(m);
    (void)computed::subtract<uint8_t>(m, m.al(), 0x49);
    if (m.flags() & emu::ZF)
        goto branch_7f19;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xE8);
    if (m.flags() & emu::ZF)
        goto branch_7f26;
    goto branch_7f33;
branch_7f19:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x3));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_beam_rifle_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_7f33;
branch_7f26:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x3));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_destructive_ray_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_7f33;
branch_7f33:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    battle_actor_pointer_core(m);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0009))));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0002))));
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x8));
    if (m.flags() & emu::ZF)
        goto branch_7f48;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::shift_right<uint8_t>(m, uint8_t(m.cx()), 0x1)));
branch_7f48:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x5);
    lookup_table_byte_core(m);
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), uint8_t(m.cx()))));
    if (!(m.flags() & emu::CF))
        goto branch_7f5a;
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() ^ m.al()))));
    // Miss when a 0..100 roll is below max(field 5 - effective attacker agility, 0).
branch_7f5a:;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(0x0));
    m.set_ah(uint8_t(0x64));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.cx()));
    if (!(m.flags() & emu::CF))
        goto branch_7f6e;
    show_no_damage_message_call_core(m);
    goto branch_7f74;
branch_7f6e:;
    {
        const uint16_t callback_cs = m.cs();
        read_target_effective_defense_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        apply_base_effect_damage_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_7f74:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_beam_weapon_action(Machine &m) {
    execute_beam_weapon_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x367F83 (near)
// Apply randomized explosive damage to the whole target group.
void execute_group_ordnance_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_7f92;
    goto branch_803c;
branch_7f92:;
    {
        const uint16_t callback_cs = m.cs();
        announce_innate_battle_attack_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_7fa0;
    goto branch_803c;
branch_7fa0:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_7fab;
    goto branch_803c;
branch_7fab:;
    selected_battle_equipment_call_core(m);
    (void)computed::subtract<uint8_t>(m, m.al(), 0x47);
    if (m.flags() & emu::ZF)
        goto branch_7fbb;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x48);
    if (m.flags() & emu::ZF)
        goto branch_8035;
    goto branch_803c;
branch_7fbb:;
    m.set_al(uint8_t(0xFF));
    m.wr8(m.ss(), uint16_t(kEffectTargetMode), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x3));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_missile_projectile_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(kEffectTargetMode), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_missile_trails_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0x4));
    m.wr8(m.ss(), uint16_t(kEffectTargetMode), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_missile_trails_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0x2));
    m.wr8(m.ss(), uint16_t(kEffectTargetMode), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_missile_trails_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0x5));
    m.wr8(m.ss(), uint16_t(kEffectTargetMode), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_missile_trails_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0x3));
    m.wr8(m.ss(), uint16_t(kEffectTargetMode), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_missile_trails_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(kEffectTargetMode), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_explosive_blast_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_explosive_blast_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0x2));
    m.wr8(m.ss(), uint16_t(kEffectTargetMode), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_explosive_blast_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_explosive_blast_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0x3));
    m.wr8(m.ss(), uint16_t(kEffectTargetMode), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_explosive_blast_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_explosive_blast_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_803c;
branch_8035:;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_nuclear_bomb_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_803c;
    // Each target gets twice (power + roll(0..field 5)), without a defense subtraction.
branch_803c:;
    {
        const uint16_t callback_cs = m.cs();
        roll_effect_power_and_spread_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.dx()));
    m.wr16(m.ss(), uint16_t(kMeterInputTarget), uint16_t(m.dx()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_8051;
    {
        const uint16_t callback_cs = m.cs();
        show_group_damage_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_8051:;
    m.dx() = uint16_t(m.rd16(m.ss(), uint16_t(kMeterInputTarget)));
    set_battle_meter_target_core(m);
    check_target_knocked_out_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_group_ordnance_action(Machine &m) {
    execute_group_ordnance_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36806B (near)
// Defeat susceptible target-group members and update the defeated-enemy display.
void execute_group_elimination_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_808f;
    {
        const uint16_t callback_cs = m.cs();
        announce_innate_battle_attack_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_808f;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_wave_cannon_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_808f;
    // Kill a non-dead/non-stoned target only when its type ID (field 1Fh) is below item field 4.
branch_808f:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x4);
    lookup_table_byte_core(m);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0002))));
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x3));
    if (!(m.flags() & emu::ZF))
        goto branch_810e;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x001F))));
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.cx()));
    if (m.flags() & emu::CF)
        goto branch_80b9;
    show_no_effect_message_call_core(m);
    goto branch_810e;
branch_80b9:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0002))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | 0x1))));
    if (!BattleActorState(m, m.si()).write_status(uint8_t(m.al()))) {
        show_no_effect_message_call_core(m);
        goto branch_810e;
    }
    m.ax() = uint16_t(0x0);
    BattleActorState(m, m.si()).write_hp(uint16_t(m.ax()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_80de;
    remove_defeated_enemy_group_core(m);
    goto branch_80e1;
branch_80de:;
    shake_screen_for_party_hit_core(m);
branch_80e1:;
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0xF));
    draw_battle_skill_list_core(m);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    draw_battle_member_panels_core(m);
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(kBattleGroupEliminationScratch), uint8_t(m.al()));
branch_810e:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_group_elimination_action(Machine &m) {
    execute_group_elimination_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36811D (near)
// Restore target HP with base item healing plus random spread.
void execute_potion_healing_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_8186;
    {
        const uint16_t callback_cs = m.cs();
        announce_used_battle_item_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_8186;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_8186;
    selected_battle_equipment_call_core(m);
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_814d;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_8160;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x80);
    if (m.flags() & emu::ZF)
        goto branch_8173;
branch_814d:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x2));
    m.set_al(uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectTargetMode), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_healing_glow_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8186;
branch_8160:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0xA));
    m.set_al(uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectTargetMode), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_healing_glow_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8186;
branch_8173:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    m.set_al(uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kEffectTargetMode), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_healing_glow_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8186;
branch_8186:;
    {
        const uint16_t callback_cs = m.cs();
        roll_base_effect_damage_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    (void)computed::subtract<uint16_t>(m, m.dx(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_8193;
    show_no_effect_message_call_core(m);
    goto branch_81c8;
    // Add power + roll(0..power/2), clamp to max HP, then report the actual HP gain.
branch_8193:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.si() + 0x0003)));
    m.cx() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.bx() = uint16_t(m.rd16(m.ss(), uint16_t(m.si() + 0x0005)));
    (void)computed::subtract<uint16_t>(m, m.ax(), m.bx());
    if (m.flags() & emu::CF)
        goto branch_81ae;
    if (m.flags() & emu::ZF)
        goto branch_81ae;
    m.ax() = uint16_t(m.bx());
branch_81ae:;
    BattleActorState(m, m.si()).write_hp(uint16_t(m.ax()));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.cx()));
    m.wr16(m.ss(), uint16_t(kMeterInputTarget), uint16_t(m.ax()));
    {
        const uint16_t callback_cs = m.cs();
        show_target_hp_recovery_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0xF));
    draw_battle_skill_list_core(m);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
branch_81c8:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_potion_healing_action(Machine &m) {
    execute_potion_healing_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x3681D7 (near)
// Clear the item's selected statuses and announce each recovered condition.
void execute_status_cure_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_81e6;
    goto branch_8278;
branch_81e6:;
    {
        const uint16_t callback_cs = m.cs();
        announce_used_battle_item_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_81f4;
    goto branch_8278;
branch_81f4:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_8278;
    selected_battle_equipment_call_core(m);
    (void)computed::subtract<uint8_t>(m, m.al(), 0x5);
    if (m.flags() & emu::ZF)
        goto branch_821d;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6);
    if (m.flags() & emu::ZF)
        goto branch_822a;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x7C);
    if (m.flags() & emu::ZF)
        goto branch_8251;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x7D);
    if (m.flags() & emu::ZF)
        goto branch_825e;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x84);
    if (m.flags() & emu::ZF)
        goto branch_826b;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x7A);
    if (m.flags() & emu::ZF)
        goto branch_8237;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x7B);
    if (m.flags() & emu::ZF)
        goto branch_8244;
branch_821d:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_status_cure_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8278;
branch_822a:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x2));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_status_cure_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8278;
branch_8237:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_status_cure_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8278;
branch_8244:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x3));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_status_cure_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8278;
branch_8251:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x5));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_status_cure_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8278;
branch_825e:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x9));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_status_cure_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8278;
branch_826b:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_status_cure_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8278;
branch_8278:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x4);
    lookup_table_byte_core(m);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0002))));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & uint8_t(m.cx())))));
    BattleActorState(m, m.si()).write_status(uint8_t(m.al()));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(~uint8_t(m.cx())));
    m.set_al(uint8_t(uint8_t(m.cx())));
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & uint8_t(m.dx())));
    if (!(m.flags() & emu::ZF))
        goto branch_82a4;
    goto branch_8345;
branch_82a4:;
    (void)computed::logic<uint8_t>(m, uint8_t(uint8_t(m.dx()) & 0x20));
    if (m.flags() & emu::ZF)
        goto branch_8304;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0002))));
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x20));
    if (!(m.flags() & emu::ZF))
        goto branch_8304;
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() ^ m.bx())));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x0));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x0)) << 8));
branch_82c1:;
    m.wr8(m.ss(), uint16_t(m.bx() + kEquipmentRow), uint8_t(uint8_t(m.cx())));
    m.wr8(m.ss(), uint16_t(m.bx() + kEquipmentPage), uint8_t(uint8_t(m.cx() >> 8)));
    m.ax() = uint16_t(m.bx());
    selected_battle_equipment_call_core(m);
    (void)computed::subtract<uint8_t>(m, m.ah(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_82ee;
    (void)computed::subtract<uint8_t>(m, m.ah(), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_82ee;
    computed::push(m, m.bx());
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.bx() = uint16_t(0x0);
    lookup_table_byte_core(m);
    m.bx() = uint16_t(computed::pop(m));
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x1));
    if (m.flags() & emu::ZF)
        goto branch_82ee;
    goto branch_8300;
branch_82ee:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_82f7;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()))));
    goto branch_82f9;
branch_82f7:;
    m.cx() = uint16_t((m.cx() & 0x00FF) |
                      (uint16_t(uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx() >> 8)))) << 8));
branch_82f9:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx() >> 8), 0x6);
    if (m.flags() & emu::ZF)
        goto branch_8300;
    goto branch_82c1;
branch_8300:;
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
branch_8304:;
    m.dx() = uint16_t((m.dx() & 0xFF00) |
                      uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.dx()) & uint8_t(m.cx())))));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x0));
branch_8308:;
    m.dx() = uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(0x1)) << 8));
    m.dx() = uint16_t(
        (m.dx() & 0x00FF) |
        (uint16_t(uint8_t(computed::shift_left<uint8_t>(m, uint8_t(m.dx() >> 8), uint8_t(m.cx())))) << 8));
    m.set_al(uint8_t(uint8_t(m.dx())));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & uint8_t(m.dx() >> 8)))));
    if (m.flags() & emu::ZF)
        goto branch_8315;
    {
        const uint16_t callback_cs = m.cs();
        show_cured_status_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_8315:;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()))));
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x8);
    if (!(m.flags() & emu::ZF))
        goto branch_8308;
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0xF));
    draw_battle_skill_list_core(m);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    draw_battle_member_panels_core(m);
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    goto branch_8348;
branch_8345:;
    show_no_effect_message_call_core(m);
branch_8348:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_status_cure_action(Machine &m) {
    execute_status_cure_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x368357 (near)
// Choose the current elemental spell's sprite animation, accounting for reflection.
void animate_elemental_damage_action_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_8362;
    goto branch_84d6;
branch_8362:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_836d;
    goto branch_84d6;
branch_836d:;
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kBattleEffectReflected))));
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_837e;
    selected_battle_equipment_call_core(m);
    goto branch_8382;
branch_837e:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
branch_8382:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6A);
    if (m.flags() & emu::ZF)
        goto branch_83fb;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xC6);
    if (m.flags() & emu::ZF)
        goto branch_83fb;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xB9);
    if (m.flags() & emu::ZF)
        goto branch_83ed;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x67);
    if (!(m.flags() & emu::ZF))
        goto branch_8395;
    goto branch_8487;
branch_8395:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6B);
    if (!(m.flags() & emu::ZF))
        goto branch_839c;
    goto branch_842d;
branch_839c:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xC7);
    if (!(m.flags() & emu::ZF))
        goto branch_83a3;
    goto branch_842d;
branch_83a3:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xBA);
    if (m.flags() & emu::ZF)
        goto branch_841f;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6C);
    if (!(m.flags() & emu::ZF))
        goto branch_83ae;
    goto branch_8440;
branch_83ae:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xBB);
    if (!(m.flags() & emu::ZF))
        goto branch_83b5;
    goto branch_847a;
branch_83b5:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xC8);
    if (!(m.flags() & emu::ZF))
        goto branch_83bc;
    goto branch_845d;
branch_83bc:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6D);
    if (!(m.flags() & emu::ZF))
        goto branch_83c3;
    goto branch_84b9;
branch_83c3:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xBC);
    if (!(m.flags() & emu::ZF))
        goto branch_83ca;
    goto branch_849f;
branch_83ca:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xBD);
    if (!(m.flags() & emu::ZF))
        goto branch_83d1;
    goto branch_84ac;
branch_83d1:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xBE);
    if (m.flags() & emu::ZF)
        goto branch_83df;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x67);
    if (!(m.flags() & emu::ZF))
        goto branch_83dc;
    goto branch_8487;
branch_83dc:;
    goto branch_84d6;
branch_83df:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x2));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_liquid_jet_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_84d6;
branch_83ed:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_flame_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_84d6;
branch_83fb:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_spell_casting_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_fire_spell_opening_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_fire_spell_flash_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_84d6;
branch_841f:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_cold_breath_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_84d6;
branch_842d:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_spell_casting_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_blizzard_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_84d6;
branch_8440:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_spell_casting_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_thunder_spell_opening_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_thunder_spell_flash_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_84d6;
branch_845d:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_spell_casting_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_thunder_spell_opening_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_thunder_spell_flash_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_84d6;
branch_847a:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_innate_lightning_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_84d6;
branch_8487:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_spell_casting_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_flame_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_84d6;
branch_849f:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x5));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_battle_effect_7_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_84d6;
branch_84ac:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x5));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_liquid_jet_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_84d6;
branch_84b9:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_spell_casting_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_cloud_spell_opening_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_cloud_spell_flash_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_84d6;
branch_84d6:;
    return;
}
void animate_elemental_damage_action(Machine &m) {
    animate_elemental_damage_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x3684D7 (near)
// Resolve reflected elemental magic with caster magic, target resistance and damage display.
void execute_elemental_magic_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_84e9;
    announce_battle_action_call_core(m);
    {
        const uint16_t callback_cs = m.cs();
        animate_elemental_damage_action_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_84e9:;
    {
        const uint16_t callback_cs = m.cs();
        prepare_reflected_battle_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kTextColorScratch))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_8570;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleEffectReflected))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_84ff;
    {
        const uint16_t callback_cs = m.cs();
        animate_elemental_damage_action_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_84ff:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x000C))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & uint8_t(m.cx())))));
    if (m.flags() & emu::ZF)
        goto branch_8512;
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x0));
    goto branch_8516;
branch_8512:;
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x000A))));
    // P = power * floor(caster magic / 4); raw amount = P + magic + roll(0..low8(P)/2).
branch_8516:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x000A))));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(computed::shift_right<uint8_t>(m, m.al(), 0x2)));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.al()));
    computed::push(m, m.bx());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x4);
    lookup_table_byte_core(m);
    m.bx() = uint16_t(computed::pop(m));
    {
        const uint16_t product = uint16_t(m.al()) * uint8_t(m.dx());
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.dx() = uint16_t(m.ax());
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(uint8_t(m.dx()))) << 8));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(uint8_t(m.cx())));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.ax()));
    m.set_ah(uint8_t(uint8_t(m.bx() >> 8)));
    m.set_ah(uint8_t(computed::shift_right<uint8_t>(m, m.ah(), 0x1)));
    {
        const uint16_t callback_cs = m.cs();
        add_random_damage_bonus_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    // Original quirk: subtract target magic only to test for positive damage; the applied damage restores raw
    // DX, then doubles it.
    computed::push(m, m.dx());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(uint8_t(m.bx())));
    m.dx() = uint16_t(computed::subtract<uint16_t>(m, m.dx(), m.ax()));
    (void)computed::logic<uint8_t>(m, uint8_t(uint8_t(m.dx() >> 8) & 0x80));
    if (!(m.flags() & emu::ZF))
        goto branch_8559;
    m.dx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.dx() | m.dx())));
    if (!(m.flags() & emu::ZF))
        goto branch_855f;
branch_8559:;
    m.dx() = uint16_t(computed::pop(m));
    show_no_damage_message_call_core(m);
    goto branch_8570;
branch_855f:;
    m.dx() = uint16_t(computed::pop(m));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.dx()));
    m.wr16(m.ss(), uint16_t(kMeterInputTarget), uint16_t(m.dx()));
    set_battle_meter_target_core(m);
    open_party_member_popup_core(m);
    check_target_knocked_out_core(m);
branch_8570:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleEffectReflected))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_859a;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    m.dx() = uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget)))) << 8));
    m.wr8(m.ss(), uint16_t(kActingActor), uint8_t(uint8_t(m.dx() >> 8)));
    m.wr8(m.ss(), uint16_t(kActionTarget), uint8_t(m.al()));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x001F))));
    party_slot_for_name_id_core(m);
    m.wr8(m.ss(), uint16_t(kTargetGroup), uint8_t(m.al()));
branch_859a:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_elemental_magic_action(Machine &m) {
    execute_elemental_magic_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x3685A9 (near)
// Choose the current status spell's animation, accounting for reflection.
void animate_status_magic_action_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_85b4;
    goto branch_8814;
branch_85b4:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_85bf;
    goto branch_8814;
branch_85bf:;
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kBattleEffectReflected))));
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_85d0;
    selected_battle_equipment_call_core(m);
    goto branch_85d4;
branch_85d0:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
branch_85d4:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xA6);
    if (!(m.flags() & emu::ZF))
        goto branch_85db;
    goto branch_86d8;
branch_85db:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xE6);
    if (!(m.flags() & emu::ZF))
        goto branch_85e2;
    goto branch_874a;
branch_85e2:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xEC);
    if (!(m.flags() & emu::ZF))
        goto branch_85e9;
    goto branch_8718;
branch_85e9:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xF6);
    if (!(m.flags() & emu::ZF))
        goto branch_85f0;
    goto branch_86ff;
branch_85f0:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xA8);
    if (!(m.flags() & emu::ZF))
        goto branch_85f7;
    goto branch_86e6;
branch_85f7:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xCB);
    if (!(m.flags() & emu::ZF))
        goto branch_85fe;
    goto branch_8731;
branch_85fe:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xDC);
    if (!(m.flags() & emu::ZF))
        goto branch_8605;
    goto branch_8758;
branch_8605:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xA2);
    if (!(m.flags() & emu::ZF))
        goto branch_860c;
    goto branch_8771;
branch_860c:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xA3);
    if (!(m.flags() & emu::ZF))
        goto branch_8613;
    goto branch_877f;
branch_8613:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xA4);
    if (!(m.flags() & emu::ZF))
        goto branch_861a;
    goto branch_878d;
branch_861a:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xA5);
    if (!(m.flags() & emu::ZF))
        goto branch_8621;
    goto branch_879a;
branch_8621:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xAD);
    if (!(m.flags() & emu::ZF))
        goto branch_8628;
    goto branch_87a7;
branch_8628:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xAE);
    if (!(m.flags() & emu::ZF))
        goto branch_862f;
    goto branch_87b4;
branch_862f:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x78);
    if (!(m.flags() & emu::ZF))
        goto branch_8636;
    goto branch_87cc;
branch_8636:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xF1);
    if (!(m.flags() & emu::ZF))
        goto branch_863d;
    goto branch_87de;
branch_863d:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xA9);
    if (!(m.flags() & emu::ZF))
        goto branch_8644;
    goto branch_87de;
branch_8644:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x68);
    if (!(m.flags() & emu::ZF))
        goto branch_864b;
    goto branch_8802;
branch_864b:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xCA);
    if (m.flags() & emu::ZF)
        goto branch_86b4;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6F);
    if (m.flags() & emu::ZF)
        goto branch_869b;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x70);
    if (m.flags() & emu::ZF)
        goto branch_8682;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x71);
    if (m.flags() & emu::ZF)
        goto branch_865e;
    goto branch_8814;
branch_865e:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_spell_casting_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x4));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_battle_effect_7_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x4));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_death_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8814;
branch_8682:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_spell_casting_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x6));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_formation_palette_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8814;
branch_869b:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_spell_casting_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_battle_effect_7_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8814;
branch_86b4:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_spell_casting_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x4));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_battle_effect_7_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x4));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_death_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8814;
branch_86d8:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x4));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_liquid_jet_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8814;
branch_86e6:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_formation_palette_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x3));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_curse_eye_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8814;
branch_86ff:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_formation_palette_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_battle_effect_6_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8814;
branch_8718:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_telekinesis_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_battle_effect_6_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8814;
branch_8731:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_spell_casting_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_battle_effect_7_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8814;
branch_874a:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x5));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_battle_effect_7_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8814;
branch_8758:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_spell_casting_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x6));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_formation_palette_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8814;
branch_8771:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_battle_effect_7_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8814;
branch_877f:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_battle_effect_7_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8814;
branch_878d:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x4));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_battle_effect_7_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8814;
branch_879a:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_formation_palette_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8814;
branch_87a7:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x6));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_formation_palette_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8814;
branch_87b4:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_formation_palette_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x4));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_death_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8814;
branch_87cc:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_hypnotic_gaze_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_hypnotic_gaze_portraits_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8814;
branch_87de:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_hypnotic_gaze_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_hypnotic_gaze_portraits_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8814;
branch_8802:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_hypnotic_gaze_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_hypnotic_gaze_portraits_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8814;
branch_8814:;
    return;
}
void animate_status_magic_action(Machine &m) {
    animate_status_magic_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x368815 (near)
// Apply reflected status magic after immunity and magic-based success checks.
void execute_status_magic_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_8827;
    announce_battle_action_call_core(m);
    {
        const uint16_t callback_cs = m.cs();
        animate_status_magic_action_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_8827:;
    {
        const uint16_t callback_cs = m.cs();
        prepare_reflected_battle_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kTextColorScratch))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (!(m.flags() & emu::ZF))
        goto branch_8835;
    goto branch_890a;
branch_8835:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleEffectReflected))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_8840;
    {
        const uint16_t callback_cs = m.cs();
        animate_status_magic_action_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_8840:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x000C))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & uint8_t(m.cx())))));
    if (m.flags() & emu::ZF)
        goto branch_8854;
    m.dx() = uint16_t(0x0);
    goto branch_886d;
    // Failure threshold = byte(target magic + 20) - caster magic; a nonpositive threshold always succeeds.
branch_8854:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x000A))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x14)));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.al()));
    m.dx() = uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(0x0)) << 8));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x000A))));
    m.set_ah(uint8_t(0x0));
    m.dx() = uint16_t(computed::subtract<uint16_t>(m, m.dx(), m.ax()));
branch_886d:;
    (void)computed::subtract<uint16_t>(m, m.dx(), 0x0);
    if ((m.flags() & emu::ZF) || (bool(m.flags() & emu::SF) != bool(m.flags() & emu::OF)))
        goto branch_8885;
    m.set_al(uint8_t(0x0));
    m.set_ah(uint8_t(0x64));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.dx()));
    if (!(m.flags() & emu::CF))
        goto branch_8885;
    show_no_effect_message_call_core(m);
    goto branch_890a;
branch_8885:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x4);
    lookup_table_byte_core(m);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0002))));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.al()));
    (void)computed::logic<uint8_t>(m, uint8_t(uint8_t(m.cx()) & uint8_t(m.dx())));
    if (!(m.flags() & emu::ZF))
        goto branch_88b1;
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | uint8_t(m.cx())))));
    if (!BattleActorState(m, m.si()).write_status(uint8_t(m.al()))) {
        goto branch_88b1;
    }
    m.set_al(uint8_t(uint8_t(m.cx())));
    show_status_inflicted_message_core(m);
    goto branch_88b6;
branch_88b1:;
    show_no_effect_message_call_core(m);
    goto branch_890a;
branch_88b6:;
    (void)computed::logic<uint8_t>(m, uint8_t(uint8_t(m.cx()) & 0x3));
    if (m.flags() & emu::ZF)
        goto branch_88d1;
    (void)computed::logic<uint8_t>(m, uint8_t(uint8_t(m.cx()) & 0x1));
    if (m.flags() & emu::ZF)
        goto branch_88c7;
    m.ax() = uint16_t(0x0);
    BattleActorState(m, m.si()).write_hp(uint16_t(m.ax()));
branch_88c7:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleStatusSuccessCount))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x1)));
    m.wr8(m.ss(), uint16_t(kBattleStatusSuccessCount), uint8_t(m.al()));
branch_88d1:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_88e0;
    computed::push(m, m.cx());
    remove_defeated_enemy_group_core(m);
    m.cx() = uint16_t(computed::pop(m));
    goto branch_88e3;
branch_88e0:;
    shake_screen_for_party_hit_core(m);
branch_88e3:;
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0xF));
    draw_battle_skill_list_core(m);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    draw_battle_member_panels_core(m);
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
branch_890a:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleEffectReflected))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_8934;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    m.dx() = uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget)))) << 8));
    m.wr8(m.ss(), uint16_t(kActingActor), uint8_t(uint8_t(m.dx() >> 8)));
    m.wr8(m.ss(), uint16_t(kActionTarget), uint8_t(m.al()));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x001F))));
    party_slot_for_name_id_core(m);
    m.wr8(m.ss(), uint16_t(kTargetGroup), uint8_t(m.al()));
branch_8934:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_status_magic_action(Machine &m) {
    execute_status_magic_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x368943 (near)
// Damage susceptible targets or announce that the scripture has no effect on them.
void execute_scripture_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_8977;
    announce_battle_action_call_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_8977;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_8977;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_scripture_opening_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_scripture_flash_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_scripture_closing_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
branch_8977:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x5);
    lookup_table_byte_core(m);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0001))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & uint8_t(m.cx())))));
    if (!(m.flags() & emu::ZF))
        goto branch_8999;
    {
        const uint16_t callback_cs = m.cs();
        show_scripture_ineffective_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    goto branch_89cd;
    // Against matching species: damage = power * floor(caster magic / 4) + magic + roll(0..magic).
branch_8999:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    battle_actor_pointer_core(m);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x000A))));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x4);
    lookup_table_byte_core(m);
    m.set_ah(uint8_t(uint8_t(m.cx())));
    m.set_ah(uint8_t(computed::shift_right<uint8_t>(m, m.ah(), 0x2)));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.dx() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(uint8_t(m.cx())));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.ax()));
    m.set_ah(uint8_t(uint8_t(m.cx())));
    {
        const uint16_t callback_cs = m.cs();
        add_random_damage_bonus_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    set_battle_meter_target_core(m);
    open_party_member_popup_core(m);
    check_target_knocked_out_core(m);
branch_89cd:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_scripture_action(Machine &m) {
    execute_scripture_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x3689DC (near)
// Resolve magic-weapon accuracy and power with guarding, resistance and reaction effects.
void execute_magic_weapon_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_89eb;
    goto branch_8a6b;
branch_89eb:;
    announce_battle_action_call_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_8a6b;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_8a6b;
    selected_battle_equipment_call_core(m);
    (void)computed::subtract<uint8_t>(m, m.al(), 0x72);
    if (m.flags() & emu::ZF)
        goto branch_8a1d;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x73);
    if (m.flags() & emu::ZF)
        goto branch_8a2a;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x74);
    if (m.flags() & emu::ZF)
        goto branch_8a37;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x75);
    if (m.flags() & emu::ZF)
        goto branch_8a44;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x76);
    if (m.flags() & emu::ZF)
        goto branch_8a51;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x77);
    if (m.flags() & emu::ZF)
        goto branch_8a5e;
    goto branch_8a6b;
branch_8a1d:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_impact_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8a6b;
branch_8a2a:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_impact_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8a6b;
branch_8a37:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_impact_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8a6b;
branch_8a44:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_impact_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8a6b;
branch_8a51:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_spinning_strike_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8a6b;
branch_8a5e:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_spinning_strike_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8a6b;
branch_8a6b:;
    {
        const uint16_t callback_cs = m.cs();
        read_effective_battle_agilities_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    battle_actor_pointer_core(m);
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xE));
    m.bx() = uint16_t(0x0);
    m.cx() = uint16_t(0x0);
branch_8a80:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bx() + m.si()))));
    m.set_ah(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    (void)computed::subtract<uint8_t>(m, m.al(), m.ah());
    if (m.flags() & emu::ZF)
        goto branch_8a9b;
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), 0x2));
    m.cx() = uint16_t(computed::add<uint16_t>(m, m.cx(), 0x1));
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x8);
    if (!(m.flags() & emu::ZF))
        goto branch_8a80;
    m.set_al(uint8_t(0x80));
    goto branch_8a9f;
branch_8a9b:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bx() + m.si() + 0x0001))));
    // Use the held weapon uses byte as wear (zero if bit 7 is set); a missing weapon also gives zero wear.
branch_8a9f:;
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x80));
    if (m.flags() & emu::ZF)
        goto branch_8aa7;
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() ^ m.al()))));
    // Accuracy starts with byte(wear + target agility); the extra-effect path instead overwrites BX with 5.
branch_8aa7:;
    m.wr8(m.ss(), uint16_t(kBattleItemPower), uint8_t(m.al()));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), uint8_t(m.bx()))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    has_table_flag_bit1_core(m);
    if (!(m.flags() & emu::CF))
        goto branch_8ac6;
    if (m.flags() & emu::ZF)
        goto branch_8ac6;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionEffect))));
    m.bx() = uint16_t(0x5);
    lookup_table_byte_core(m);
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), uint8_t(m.bx()))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    // Miss threshold = max(adjusted target value - attacker agility - 99, 0), with byte arithmetic.
branch_8ac6:;
    m.set_al(uint8_t(uint8_t(m.bx())));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), uint8_t(m.cx()))));
    if (m.flags() & emu::CF)
        goto branch_8ad0;
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x63)));
    if (!(m.flags() & emu::CF))
        goto branch_8ad2;
branch_8ad0:;
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() ^ m.al()))));
branch_8ad2:;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0x6400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.cx()));
    if (!(m.flags() & emu::CF))
        goto branch_8ae5;
    show_attack_missed_message_call_core(m);
    goto branch_8b5e;
    // Damage starts at power + roll(0..field 5) + attacker agility + 99 - wear; zero wear triples it.
branch_8ae5:;
    {
        const uint16_t callback_cs = m.cs();
        read_target_effective_defense_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(uint8_t(m.cx())));
    computed::push(m, m.bx());
    {
        const uint16_t callback_cs = m.cs();
        read_effective_battle_agilities_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.bx() = uint16_t(computed::pop(m));
    {
        const uint16_t callback_cs = m.cs();
        roll_effect_power_and_spread_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(uint8_t(m.cx())));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.ax()));
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.set_al(uint8_t(0x63));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.ax()));
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleItemPower))));
    m.dx() = uint16_t(computed::subtract<uint16_t>(m, m.dx(), m.ax()));
    if (!(m.flags() & emu::CF))
        goto branch_8b0b;
    m.dx() = uint16_t(0x0);
branch_8b0b:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_8b15;
    m.ax() = uint16_t(m.dx());
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.dx()));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.ax()));
branch_8b15:;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(uint8_t(m.bx())));
    m.dx() = uint16_t(computed::subtract<uint16_t>(m, m.dx(), m.ax()));
    if (m.flags() & emu::CF)
        goto branch_8b25;
    {
        const uint16_t callback_cs = m.cs();
        reduce_signed_damage_for_guard_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    (void)computed::subtract<uint16_t>(m, m.dx(), 0x0);
    if (!(m.flags() & emu::CF))
        goto branch_8b2a;
branch_8b25:;
    show_no_damage_message_call_core(m);
    goto branch_8b5e;
branch_8b2a:;
    set_battle_meter_target_core(m);
    open_party_member_popup_core(m);
    check_target_knocked_out_core(m);
    if (m.flags() & emu::CF)
        goto branch_8b5e;
    has_table_flag_bit1_core(m);
    if (m.flags() & emu::CF)
        goto branch_8b3c;
    goto branch_8b5e;
branch_8b3c:;
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x2));
    if (!(m.flags() & emu::ZF))
        goto branch_8b4a;
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x4));
    if (!(m.flags() & emu::ZF))
        goto branch_8b54;
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x10));
    if (!(m.flags() & emu::ZF))
        goto branch_8b59;
    goto branch_8b5e;
branch_8b4a:;
    resolve_drain_hit_call_core(m);
    goto branch_8b5e;
branch_8b54:;
    resolve_status_hit_call_core(m);
    goto branch_8b5e;
branch_8b59:;
    resolve_fixed_damage_hit_call_core(m);
    goto branch_8b5e;
branch_8b5e:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_magic_weapon_action(Machine &m) {
    execute_magic_weapon_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x368B6D (near)
// Roll repeated physical hits and report the combined damage and hit count.
void execute_multi_hit_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_8b7c;
    goto branch_8c24;
branch_8b7c:;
    announce_battle_action_call_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_8b8a;
    goto branch_8c24;
branch_8b8a:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_8b95;
    goto branch_8c24;
branch_8b95:;
    selected_battle_equipment_call_core(m);
    (void)computed::subtract<uint8_t>(m, m.al(), 0x93);
    if (m.flags() & emu::ZF)
        goto branch_8bbc;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x94);
    if (m.flags() & emu::ZF)
        goto branch_8bc9;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x96);
    if (m.flags() & emu::ZF)
        goto branch_8bd6;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x97);
    if (m.flags() & emu::ZF)
        goto branch_8be3;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x98);
    if (m.flags() & emu::ZF)
        goto branch_8bf0;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x99);
    if (m.flags() & emu::ZF)
        goto branch_8bfd;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x9A);
    if (m.flags() & emu::ZF)
        goto branch_8c0a;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xF7);
    if (m.flags() & emu::ZF)
        goto branch_8c17;
    goto branch_8c24;
branch_8bbc:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x2));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_battle_effect_4_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8c24;
branch_8bc9:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x3));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_impact_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8c24;
branch_8bd6:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_impact_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8c24;
branch_8be3:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x4));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_battle_effect_5_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8c24;
branch_8bf0:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_impact_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8c24;
branch_8bfd:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_hit_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8c24;
branch_8c0a:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x3));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_impact_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8c24;
branch_8c17:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x3));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_impact_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8c24;
branch_8c24:;
    {
        const uint16_t callback_cs = m.cs();
        compute_agility_hit_threshold_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x0));
    computed::push(m, m.bx());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x5);
    lookup_table_byte_core(m);
    m.bx() = uint16_t(computed::pop(m));
    m.dx() = uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    // Roll field-5 hit attempts (zero wraps to 256 attempts); each 0..100 roll >= agility threshold counts.
branch_8c3b:;
    m.ax() = uint16_t(0x6400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.cx()));
    if (m.flags() & emu::CF)
        goto branch_8c49;
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.dx()))));
branch_8c49:;
    m.dx() = uint16_t((m.dx() & 0x00FF) |
                      (uint16_t(uint8_t(computed::increment<uint8_t>(m, uint8_t(m.dx() >> 8), true))) << 8));
    if (!(m.flags() & emu::ZF))
        goto branch_8c3b;
    m.set_al(uint8_t(uint8_t(m.dx())));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | m.al()))));
    if (!(m.flags() & emu::ZF))
        goto branch_8c59;
    show_attack_missed_message_call_core(m);
    goto branch_8d14;
branch_8c59:;
    m.wr8(m.ss(), uint16_t(kBattleMultiHitCount), uint8_t(m.al()));
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.wr16(m.ss(), uint16_t(kMeterInputTarget), uint16_t(m.ax()));
    {
        const uint16_t callback_cs = m.cs();
        show_multi_hit_count_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        read_target_effective_defense_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(uint8_t(m.cx())));
    m.wr8(m.ss(), uint16_t(kBattleSavedAttackValue), uint8_t(m.al()));
    {
        const uint16_t callback_cs = m.cs();
        read_attacker_effective_strength_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(uint8_t(m.bx())));
    // Original quirk: the saved bonus is BL (target agility); the strength helper returns strength in CL.
    m.wr8(m.ss(), uint16_t(kBattleItemPower), uint8_t(m.al()));
    computed::push(m, m.bx());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x4);
    lookup_table_byte_core(m);
    m.bx() = uint16_t(computed::pop(m));
    // P = power * floor(effective strength / 4); reuse this base for every successful hit.
    m.set_ah(uint8_t(uint8_t(m.cx())));
    m.set_ah(uint8_t(computed::shift_right<uint8_t>(m, m.ah(), 0x2)));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.dx() = uint16_t(m.ax());
    m.bx() = uint16_t(0x0);
    // Each hit = max(P + saved bonus + roll(0..low8(P + bonus)/2) - defense, 0), but 1 also becomes zero.
branch_8c92:;
    computed::push(m, m.dx());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleItemPower))));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.ax()));
    m.set_ah(uint8_t(uint8_t(m.dx())));
    m.set_ah(uint8_t(computed::shift_right<uint8_t>(m, m.ah(), 0x1)));
    m.set_al(uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.set_ah(uint8_t(0x0));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.ax()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleSavedAttackValue))));
    m.dx() = uint16_t(computed::subtract<uint16_t>(m, m.dx(), m.ax()));
    (void)computed::subtract<uint16_t>(m, m.dx(), 0x1);
    if (!(m.flags() & emu::ZF) && (bool(m.flags() & emu::SF) == bool(m.flags() & emu::OF)))
        goto branch_8cb8;
    m.dx() = uint16_t(0x0);
branch_8cb8:;
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), m.dx()));
    m.dx() = uint16_t(computed::pop(m));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleMultiHitCount))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ss(), uint16_t(kBattleMultiHitCount), uint8_t(m.al()));
    if (!(m.flags() & emu::ZF))
        goto branch_8c92;
    // Sum hit damage with word wrapping, then apply resistance and guard to the total (again rejecting 1).
    m.dx() = uint16_t(m.bx());
    {
        const uint16_t callback_cs = m.cs();
        reduce_signed_damage_for_guard_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    (void)computed::subtract<uint16_t>(m, m.dx(), 0x1);
    if (!(m.flags() & emu::ZF) && (bool(m.flags() & emu::SF) == bool(m.flags() & emu::OF)))
        goto branch_8ce0;
    show_no_damage_message_call_core(m);
    m.dx() = uint16_t(0x0);
    m.wr16(m.ss(), uint16_t(kMeterInputTarget), uint16_t(0x0));
    goto branch_8d05;
branch_8ce0:;
    set_battle_meter_target_core(m);
    open_party_member_popup_core(m);
    check_target_knocked_out_core(m);
    if (m.flags() & emu::CF)
        goto branch_8d14;
    has_table_flag_bit1_core(m);
    if (m.flags() & emu::CF)
        goto branch_8cf2;
    goto branch_8d14;
branch_8cf2:;
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x2));
    if (!(m.flags() & emu::ZF))
        goto branch_8d00;
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x4));
    if (!(m.flags() & emu::ZF))
        goto branch_8d0a;
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x10));
    if (!(m.flags() & emu::ZF))
        goto branch_8d0f;
    goto branch_8d14;
branch_8d00:;
    resolve_drain_hit_call_core(m);
    goto branch_8d14;
branch_8d05:;
    resolve_counterattack_core(m);
    goto branch_8d14;
branch_8d0a:;
    resolve_status_hit_call_core(m);
    goto branch_8d14;
branch_8d0f:;
    resolve_fixed_damage_hit_call_core(m);
    goto branch_8d14;
branch_8d14:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_multi_hit_action(Machine &m) {
    execute_multi_hit_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x368D23 (near)
// Resolve an agility-based status attack and its reactions.
void execute_physical_status_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_8d32;
    goto branch_8df5;
branch_8d32:;
    announce_battle_action_call_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_8d40;
    goto branch_8df5;
branch_8d40:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_8d4b;
    goto branch_8df5;
branch_8d4b:;
    selected_battle_equipment_call_core(m);
    (void)computed::subtract<uint8_t>(m, m.al(), 0x9B);
    if (m.flags() & emu::ZF)
        goto branch_8d6b;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x9C);
    if (m.flags() & emu::ZF)
        goto branch_8d7d;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xA0);
    if (m.flags() & emu::ZF)
        goto branch_8dc5;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xA1);
    if (m.flags() & emu::ZF)
        goto branch_8ddd;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x9E);
    if (m.flags() & emu::ZF)
        goto branch_8d95;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x9F);
    if (m.flags() & emu::ZF)
        goto branch_8dad;
    goto branch_8df5;
branch_8d6b:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x5));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_impact_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_battle_effect_7_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8df5;
branch_8d7d:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x5));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_battle_effect_5_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x5));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_battle_effect_7_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8df5;
branch_8d95:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x7));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_battle_effect_3_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_battle_effect_6_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8df5;
branch_8dad:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x5));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_entangle_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_battle_effect_6_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8df5;
branch_8dc5:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x4));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_impact_sprite_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x6));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_formation_palette_effect_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8df5;
branch_8ddd:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x6));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_battle_effect_5_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x4));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_death_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_8df5;
branch_8df5:;
    m.set_al(uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kTextColorScratch), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kBattleImmunityMode), uint8_t(0x0));
    {
        const uint16_t callback_cs = m.cs();
        check_battle_effect_immunity_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kTextColorScratch))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (!(m.flags() & emu::ZF))
        goto branch_8e0f;
    goto branch_8f4c;
branch_8e0f:;
    {
        const uint16_t callback_cs = m.cs();
        read_effective_battle_agilities_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    has_table_flag_bit1_core(m);
    if (!(m.flags() & emu::CF))
        goto branch_8e2b;
    if (m.flags() & emu::ZF)
        goto branch_8e2b;
    computed::push(m, m.bx());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionEffect))));
    m.bx() = uint16_t(0x5);
    lookup_table_byte_core(m);
    m.bx() = uint16_t(computed::pop(m));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), uint8_t(m.cx()))));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
branch_8e2b:;
    m.set_al(uint8_t(uint8_t(m.bx())));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), uint8_t(m.cx()))));
    if (!(m.flags() & emu::CF))
        goto branch_8e33;
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() ^ m.al()))));
branch_8e33:;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0x6400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.cx()));
    if (!(m.flags() & emu::CF))
        goto branch_8e47;
    show_attack_missed_message_call_core(m);
    goto branch_8f4c;
    // Success threshold = max(byte(caster magic + 50) - target magic, 0), doubled on a field-0Ch mask match.
branch_8e47:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x000A))));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x000A))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x32)));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), uint8_t(m.cx()))));
    if (!(m.flags() & emu::CF))
        goto branch_8e67;
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() ^ m.al()))));
branch_8e67:;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x000C))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    computed::push(m, m.bx());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x5);
    lookup_table_byte_core(m);
    m.bx() = uint16_t(computed::pop(m));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & uint8_t(m.bx())))));
    if (m.flags() & emu::ZF)
        goto branch_8e8a;
    m.cx() =
        uint16_t((m.cx() & 0xFF00) | uint8_t(computed::add<uint8_t>(m, uint8_t(m.cx()), uint8_t(m.cx()))));
    // Original quirk: the random lower bound is the preceding mask result; only AH is set to 70.
branch_8e8a:;
    m.set_ah(uint8_t(0x46));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.cx()));
    if (m.flags() & emu::CF)
        goto branch_8e98;
    goto branch_8f20;
branch_8e98:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0002))));
    computed::push(m, m.bx());
    computed::push(m, m.si());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x4);
    lookup_table_byte_core(m);
    m.si() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    m.dx() = uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    (void)computed::logic<uint8_t>(m, uint8_t(uint8_t(m.dx() >> 8) & uint8_t(m.dx())));
    if (!(m.flags() & emu::ZF))
        goto branch_8ec6;
    m.dx() =
        uint16_t((m.dx() & 0xFF00) | uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.dx()) | m.al()))));
    if (!BattleActorState(m, m.si()).write_status(uint8_t(uint8_t(m.dx())))) {
        show_no_effect_message_call_core(m);
        goto branch_8f20;
    }
    m.set_al(uint8_t(uint8_t(m.dx() >> 8)));
    show_status_inflicted_message_core(m);
    goto branch_8ecc;
branch_8ec6:;
    show_no_effect_message_call_core(m);
    goto branch_8f4c;
    // Original quirk: both tests use bit 0, so petrification alone does not increment the success count here.
branch_8ecc:;
    (void)computed::logic<uint8_t>(m, uint8_t(uint8_t(m.dx() >> 8) & 0x1));
    if (m.flags() & emu::ZF)
        goto branch_8ee7;
    (void)computed::logic<uint8_t>(m, uint8_t(uint8_t(m.dx() >> 8) & 0x1));
    if (m.flags() & emu::ZF)
        goto branch_8edd;
    m.ax() = uint16_t(0x0);
    BattleActorState(m, m.si()).write_hp(uint16_t(m.ax()));
branch_8edd:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleStatusSuccessCount))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x1)));
    m.wr8(m.ss(), uint16_t(kBattleStatusSuccessCount), uint8_t(m.al()));
branch_8ee7:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_8ef4;
    remove_defeated_enemy_group_core(m);
    goto branch_8ef7;
branch_8ef4:;
    shake_screen_for_party_hit_core(m);
branch_8ef7:;
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0xF));
    draw_battle_skill_list_core(m);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    draw_battle_member_panels_core(m);
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    goto branch_8f4c;
branch_8f20:;
    show_attack_missed_message_call_core(m);
    has_table_flag_bit1_core(m);
    if (m.flags() & emu::CF)
        goto branch_8f2a;
    goto branch_8f4c;
branch_8f2a:;
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x2));
    if (!(m.flags() & emu::ZF))
        goto branch_8f38;
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x4));
    if (!(m.flags() & emu::ZF))
        goto branch_8f42;
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x10));
    if (!(m.flags() & emu::ZF))
        goto branch_8f47;
    goto branch_8f4c;
branch_8f38:;
    resolve_drain_hit_call_core(m);
    goto branch_8f4c;
branch_8f42:;
    resolve_status_hit_call_core(m);
    goto branch_8f4c;
branch_8f47:;
    resolve_fixed_damage_hit_call_core(m);
    goto branch_8f4c;
branch_8f4c:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_physical_status_action(Machine &m) {
    execute_physical_status_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x368F5E (near)
// Reduce the selected target stat by one eighth after the action's success check.
void execute_stat_reduction_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_8f6d;
    goto branch_9004;
branch_8f6d:;
    announce_battle_action_call_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_8f7b;
    goto branch_9004;
branch_8f7b:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_8f86;
    goto branch_9004;
branch_8f86:;
    selected_battle_equipment_call_core(m);
    (void)computed::subtract<uint8_t>(m, m.al(), 0xB1);
    if (m.flags() & emu::ZF)
        goto branch_8fc3;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xAF);
    if (m.flags() & emu::ZF)
        goto branch_8fa9;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xB0);
    if (m.flags() & emu::ZF)
        goto branch_8fb6;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xB2);
    if (m.flags() & emu::ZF)
        goto branch_8fd0;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xB5);
    if (m.flags() & emu::ZF)
        goto branch_8fea;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xB3);
    if (m.flags() & emu::ZF)
        goto branch_8fdd;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xB4);
    if (m.flags() & emu::ZF)
        goto branch_8ff7;
    goto branch_9004;
branch_8fa9:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x3));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_battle_effect_6_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_9004;
branch_8fb6:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x4));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_battle_effect_7_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_9004;
branch_8fc3:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x4));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_sandtrap_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_9004;
branch_8fd0:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_formation_tremble_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_9004;
branch_8fdd:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_battle_effect_6_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_9004;
branch_8fea:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_web_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_9004;
branch_8ff7:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x7));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_entangle_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_9004;
branch_9004:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x5);
    lookup_table_byte_core(m);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x4);
    lookup_table_byte_core(m);
    m.set_ah(uint8_t(0x0));
    m.si() = uint16_t(0x8F5B);
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.ax()));
    m.set_al(uint8_t(m.rd8(m.cs(), uint16_t(m.si()))));
    m.wr8(m.ss(), uint16_t(kBattleItemPower), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() ^ m.bx())));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kBattleItemPower))));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bx() + m.si()))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), uint8_t(m.cx()))));
    if (!(m.flags() & emu::CF))
        goto branch_9043;
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() ^ m.al()))));
    // Miss threshold = max(selected stat - field 5, 0); success subtracts floor(stat / 8).
branch_9043:;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0x6400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.cx()));
    if (!(m.flags() & emu::CF))
        goto branch_9056;
    show_attack_missed_message_call_core(m);
    goto branch_90ac;
branch_9056:;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() ^ m.bx())));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kBattleItemPower))));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bx() + m.si()))));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.al()));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(computed::shift_right<uint8_t>(m, uint8_t(m.dx()), 0x3)));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), uint8_t(m.dx()))));
    if (!BattleActorState(m, m.si()).write_stat(m.bx(), m.al())) {
        show_no_effect_message_call_core(m);
        goto branch_90ac;
    }
    m.bx() = uint16_t(m.ax());
    m.dx() = uint16_t(
        (m.dx() & 0x00FF) |
        (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.dx() >> 8) ^ uint8_t(m.dx() >> 8)))))
         << 8));
    m.set_al(uint8_t(uint8_t(m.dx())));
    m.wr16(m.ss(), uint16_t(kMeterInputTarget), uint16_t(m.dx()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_9088;
    goto branch_908b;
branch_9088:;
    shake_screen_for_party_hit_core(m);
branch_908b:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleItemPower))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x7);
    if (m.flags() & emu::ZF)
        goto branch_909d;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x9);
    if (m.flags() & emu::ZF)
        goto branch_90a2;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xA);
    if (m.flags() & emu::ZF)
        goto branch_90a7;
    goto branch_90ac;
branch_909d:;
    {
        const uint16_t callback_cs = m.cs();
        show_target_strength_decreased_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    goto branch_90ac;
branch_90a2:;
    {
        const uint16_t callback_cs = m.cs();
        show_target_agility_decreased_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    goto branch_90ac;
branch_90a7:;
    {
        const uint16_t callback_cs = m.cs();
        show_target_magic_or_defense_decreased_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    goto branch_90ac;
branch_90ac:;
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0xF));
    draw_battle_skill_list_core(m);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_stat_reduction_action(Machine &m) {
    execute_stat_reduction_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x3690C8 (near)
// Announce regeneration and animate it once at the start of the action.
void execute_regeneration_stance_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_90fd;
    {
        const uint16_t callback_cs = m.cs();
        announce_used_battle_item_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_90fd;
    selected_battle_equipment_call_core(m);
    (void)computed::subtract<uint8_t>(m, m.al(), 0xF0);
    if (m.flags() & emu::ZF)
        goto branch_90ea;
    goto branch_90fd;
branch_90ea:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    m.wr8(m.ss(), uint16_t(kActionTarget), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x5));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_protective_aura_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
branch_90fd:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_regeneration_stance_action(Machine &m) {
    execute_regeneration_stance_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36910C (near)
// Check immunity, compute the original magic-scaled amount, and apply it with knockout handling.
void execute_innate_magic_damage_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_911b;
    goto branch_91cf;
branch_911b:;
    {
        const uint16_t callback_cs = m.cs();
        announce_innate_battle_attack_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_9129;
    goto branch_91cf;
branch_9129:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_9134;
    goto branch_91cf;
branch_9134:;
    selected_battle_equipment_call_core(m);
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1C);
    if (m.flags() & emu::ZF)
        goto branch_915b;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xC0);
    if (m.flags() & emu::ZF)
        goto branch_91a0;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xC1);
    if (m.flags() & emu::ZF)
        goto branch_91ad;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xC2);
    if (m.flags() & emu::ZF)
        goto branch_91bf;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6E);
    if (m.flags() & emu::ZF)
        goto branch_9188;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xC9);
    if (m.flags() & emu::ZF)
        goto branch_9188;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xED);
    if (m.flags() & emu::ZF)
        goto branch_9168;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xBF);
    if (m.flags() & emu::ZF)
        goto branch_917b;
    goto branch_91cf;
branch_915b:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_earthquake_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_91cf;
branch_9168:;
    m.set_al(uint8_t(0xFF));
    m.wr8(m.ss(), uint16_t(kEffectTargetMode), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x4));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_psychoblast_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_91cf;
branch_917b:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_earthquake_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_91cf;
branch_9188:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_spell_casting_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_flare_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_91cf;
branch_91a0:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_tornado_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_91cf;
branch_91ad:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_acid_rain_droplets_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_acid_rain_splash_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_91cf;
branch_91bf:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_whirlpool_sprites_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_whirlpool_background_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
branch_91cf:;
    m.set_al(uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kTextColorScratch), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kBattleImmunityMode), uint8_t(0x1));
    {
        const uint16_t callback_cs = m.cs();
        check_battle_effect_immunity_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kTextColorScratch))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (!(m.flags() & emu::ZF))
        goto branch_91fb;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_91f6;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_91f9;
branch_91f6:;
    {
        const uint16_t callback_cs = m.cs();
        show_element_resistance_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_91f9:;
    goto branch_9259;
branch_91fb:;
    {
        const uint16_t callback_cs = m.cs();
        test_target_actor_effect_mask_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    if (!(m.flags() & emu::ZF))
        goto branch_9204;
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() ^ m.al()))));
    goto branch_920f;
branch_9204:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x000A))));
    // Original arithmetic: P = power * floor(magic / 4); BH gets low8(P)/2, but AH stays zero for the random
    // bonus.
branch_920f:;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x000A))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(uint8_t(m.bx())));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(computed::shift_right<uint8_t>(m, uint8_t(m.dx()), 0x2)));
    computed::push(m, m.bx());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x4);
    lookup_table_byte_core(m);
    m.bx() = uint16_t(computed::pop(m));
    {
        const uint16_t product = uint16_t(m.al()) * uint8_t(m.dx());
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.dx() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(uint8_t(m.dx()))) << 8));
    m.bx() = uint16_t((m.bx() & 0x00FF) |
                      (uint16_t(uint8_t(computed::shift_right<uint8_t>(m, uint8_t(m.bx() >> 8), 0x1))) << 8));
    m.set_al(uint8_t(uint8_t(m.bx())));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.ax()));
    {
        const uint16_t callback_cs = m.cs();
        add_random_damage_bonus_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    // Original quirk: target magic (when selected by the actor-ID mask) is added to damage, not subtracted.
    m.set_al(uint8_t(uint8_t(m.cx())));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.ax()));
    (void)computed::subtract<uint16_t>(m, m.dx(), 0x1);
    if (bool(m.flags() & emu::SF) == bool(m.flags() & emu::OF))
        goto branch_9250;
    show_no_damage_message_call_core(m);
    goto branch_9259;
branch_9250:;
    set_battle_meter_target_core(m);
    open_party_member_popup_core(m);
    check_target_knocked_out_core(m);
branch_9259:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_innate_magic_damage_action(Machine &m) {
    execute_innate_magic_damage_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x369268 (near)
// Eliminate matching actors and the caster, then report the self-destruction result.
void execute_self_destruct_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_9277;
    goto branch_9449;
branch_9277:;
    {
        const uint16_t callback_cs = m.cs();
        show_actor_self_destructed_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_9285;
    goto branch_9360;
branch_9285:;
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0x37E2);
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x4);
    m.di() = uint16_t(kPaletteTargetBuffer);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x4);
branch_929e:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x4);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        palette_step_toward_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_929e;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    m.wr8(m.ss(), uint16_t(kActionTarget), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_self_destruct_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(kEffectTargetMode), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_missile_trails_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0x4));
    m.wr8(m.ss(), uint16_t(kEffectTargetMode), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_missile_trails_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0x2));
    m.wr8(m.ss(), uint16_t(kEffectTargetMode), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_missile_trails_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0x5));
    m.wr8(m.ss(), uint16_t(kEffectTargetMode), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_missile_trails_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0x3));
    m.wr8(m.ss(), uint16_t(kEffectTargetMode), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_missile_trails_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(kEffectTargetMode), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_explosive_blast_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_explosive_blast_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0x2));
    m.wr8(m.ss(), uint16_t(kEffectTargetMode), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_explosive_blast_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_explosive_blast_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0x3));
    m.wr8(m.ss(), uint16_t(kEffectTargetMode), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_explosive_blast_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_explosive_blast_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    load_battle_palette_display_core(m);
    m.cx() = uint16_t(0x10);
branch_933f:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x4);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        palette_step_toward_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_933f;
    goto branch_9360;
branch_9360:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x4);
    lookup_table_byte_core(m);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kBattleStatusSuccessCount), uint8_t(0x0));
    m.set_al(uint8_t(0x0));
    battle_actor_pointer_core(m);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1B));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x0)) << 8));
    // Original quirk: compare each actor's type with decrementing CL (27..1), not the item value loaded into
    // BL.
branch_937d:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x001F))));
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.cx()));
    if (!(m.flags() & emu::ZF))
        goto branch_93f1;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0002))));
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x3));
    if (!(m.flags() & emu::ZF))
        goto branch_93f1;
    if (!BattleActorState(m, m.si()).can_write_status(1))
        goto branch_93f1;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleStatusSuccessCount))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x1)));
    m.wr8(m.ss(), uint16_t(kBattleStatusSuccessCount), uint8_t(m.al()));
    m.set_al(uint8_t(0x1));
    BattleActorState(m, m.si()).write_status(
        uint8_t(computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0002)) | m.al()))));
    m.ax() = uint16_t(0x0);
    BattleActorState(m, m.si()).write_hp(uint16_t(m.ax()));
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx() >> 8), 0x4);
    if (m.flags() & emu::CF)
        goto branch_93bd;
    m.wr8(m.ss(), uint16_t(kActionTarget), uint8_t(uint8_t(m.cx() >> 8)));
    remove_defeated_enemy_group_core(m);
    computed::push(m, m.cx());
    m.cx() = uint16_t(0x3);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    goto branch_93c5;
branch_93bd:;
    m.wr8(m.ss(), uint16_t(kActionTarget), uint8_t(uint8_t(m.cx() >> 8)));
    shake_screen_for_party_hit_core(m);
branch_93c5:;
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0xF));
    draw_battle_skill_list_core(m);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    draw_battle_member_panels_core(m);
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    computed::push(m, m.cx());
    m.cx() = uint16_t(0x3);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
branch_93f1:;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0x00FF) |
                      (uint16_t(uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx() >> 8)))) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    if (!(m.flags() & emu::ZF))
        goto branch_937d;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0002))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | 0x1))));
    BattleActorState(m, m.si()).write_status(uint8_t(m.al()));
    m.ax() = uint16_t(0x0);
    BattleActorState(m, m.si()).write_hp(uint16_t(m.ax()));
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0xF));
    draw_battle_skill_list_core(m);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    draw_battle_member_panels_core(m);
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleStatusSuccessCount))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_9446;
    show_enemies_eliminated_message_call_core(m);
    goto branch_9449;
branch_9446:;
    {
        const uint16_t callback_cs = m.cs();
        show_futile_self_destruct_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_9449:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_self_destruct_action(Machine &m) {
    execute_self_destruct_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x369458 (near)
// Roll theft success, subtract a scaled amount from the original D86Bh pointer, and show the result.
void execute_gold_theft_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_9467;
    goto branch_94f0;
branch_9467:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    battle_actor_pointer_core(m);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0009))));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0002))));
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x8));
    if (m.flags() & emu::ZF)
        goto branch_947c;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::shift_right<uint8_t>(m, uint8_t(m.cx()), 0x1)));
branch_947c:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x4);
    lookup_table_byte_core(m);
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), uint8_t(m.cx()))));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0x6400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.cx()));
    if (m.flags() & emu::CF)
        goto branch_949a;
    goto branch_94ed;
    // Original quirk: D86Bh is actor 10 fields 11h-14h, not the reward pool at DD6Bh; preserve this pointer.
branch_949a:;
    m.si() = uint16_t((kBattleActors + 0x151));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.si())));
    m.ax() =
        uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | m.rd16(m.ss(), uint16_t(m.si() + 0x0002)))));
    if (!(m.flags() & emu::ZF))
        goto branch_94a8;
    goto branch_94ed;
branch_94a8:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0009))));
    m.set_ah(uint8_t(m.al()));
    m.set_al(uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x5);
    lookup_table_byte_core(m);
    m.set_ah(uint8_t(0x0));
    m.dx() = uint16_t(m.ax());
    // Start AX at field 5, add it once per full CX countdown, and wrap the word; only CL was set by the roll.
branch_94ce:;
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_94ce;
    // Original quirk: a zero CX still executes the loop 65536 times; CH is retained from the caller.
    m.si() = uint16_t((kBattleActors + 0x151));
    m.dx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.dx() ^ m.dx())));
    m.wr16(m.ss(), uint16_t(m.si()),
           uint16_t(computed::subtract<uint16_t>(m, m.rd16(m.ss(), uint16_t(m.si())), m.ax())));
    m.wr16(m.ss(), uint16_t(m.si() + 0x0002),
           uint16_t(computed::subtract<uint16_t>(m, m.rd16(m.ss(), uint16_t(m.si() + 0x0002)), m.dx(),
                                                 bool(m.flags() & emu::CF))));
    if (!(m.flags() & emu::CF))
        goto branch_94e8;
    m.wr16(m.ss(), uint16_t(m.si()), uint16_t(m.dx()));
    m.wr16(m.ss(), uint16_t(m.si() + 0x0002), uint16_t(m.dx()));
branch_94e8:;
    {
        const uint16_t callback_cs = m.cs();
        show_gold_stolen_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    goto branch_94f0;
branch_94ed:;
    {
        const uint16_t callback_cs = m.cs();
        show_gold_theft_result_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_94f0:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_gold_theft_action(Machine &m) {
    execute_gold_theft_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x3694FF (near)
// Restore the actor's HP to maximum and announce recovery.
void execute_full_self_healing_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_9569;
    {
        const uint16_t callback_cs = m.cs();
        announce_used_battle_item_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_953e;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    selected_battle_equipment_call_core(m);
    (void)computed::subtract<uint8_t>(m, m.al(), 0x83);
    if (m.flags() & emu::ZF)
        goto branch_9525;
    goto branch_953e;
branch_9525:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    m.wr8(m.ss(), uint16_t(kActionTarget), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kTargetGroup), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x9));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_full_healing_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_953e;
branch_953e:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    battle_actor_pointer_core(m);
    m.bx() = uint16_t(m.rd16(m.ss(), uint16_t(m.si() + 0x0005)));
    BattleActorState(m, m.si()).write_hp(uint16_t(m.bx()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    m.wr8(m.ss(), uint16_t(kActionTarget), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kTargetGroup), uint8_t(m.al()));
    {
        const uint16_t callback_cs = m.cs();
        show_actor_hp_restored_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0xF));
    draw_battle_skill_list_core(m);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
branch_9569:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_full_self_healing_action(Machine &m) {
    execute_full_self_healing_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x369578 (near)
// Reject human or mutant targets, then apply randomized base damage after accuracy checks.
void execute_nonhuman_only_damage_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_95a4;
    announce_battle_action_call_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_95a4;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_95a4;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_destructive_ray_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_95a4;
branch_95a4:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x001F))));
    computed::push(m, m.es());
    computed::push(m, m.di());
    battle_table_pointer_core(m);
    m.set_al(uint8_t(table_at(m, Table::monsters, m.es(), uint16_t(m.di()))));
    m.di() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0xC0));
    if (m.flags() & emu::ZF)
        goto branch_95c2;
    show_no_effect_message_call_core(m);
    goto branch_9600;
branch_95c2:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0009))));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x5);
    lookup_table_byte_core(m);
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), uint8_t(m.cx()))));
    if (!(m.flags() & emu::CF))
        goto branch_95e1;
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() ^ m.al()))));
branch_95e1:;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0x6400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.cx()));
    if (!(m.flags() & emu::CF))
        goto branch_95f4;
    show_attack_missed_message_call_core(m);
    goto branch_9600;
branch_95f4:;
    {
        const uint16_t callback_cs = m.cs();
        roll_base_effect_damage_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    set_battle_meter_target_core(m);
    open_party_member_popup_core(m);
    check_target_knocked_out_core(m);
branch_9600:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_nonhuman_only_damage_action(Machine &m) {
    execute_nonhuman_only_damage_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36960F (near)
// Damage every eligible actor except the caster, count targets/knockouts, and heal by target count times
// power.
void execute_group_drain_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_961e;
    goto branch_991a;
branch_961e:;
    {
        const uint16_t callback_cs = m.cs();
        announce_used_battle_item_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_962c;
    goto branch_97ac;
branch_962c:;
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0x38C2);
    m.ax() = uint16_t(0x4);
    m.cx() = uint16_t(0x3);
    m.di() = uint16_t(kPaletteTargetBuffer);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x8000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0x37E2);
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x4);
    m.di() = uint16_t(kPaletteTargetBuffer);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x8);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x0));
branch_965d:;
    computed::push(m, m.ax());
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x8)));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.dx()));
    m.ax() = uint16_t(computed::pop(m));
    if (m.flags() & emu::ZF)
        goto branch_968c;
    computed::push(m, m.ax());
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x8)));
    battle_actor_pointer_core(m);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0002))));
    (void)computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx()) & 0x3));
    m.ax() = uint16_t(computed::pop(m));
    if (!(m.flags() & emu::ZF))
        goto branch_968c;
    computed::push(m, m.ax());
    m.cx() = uint16_t(0x1);
    m.si() = uint16_t(0x38C2);
    m.di() = uint16_t(kPaletteTargetBuffer);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(computed::pop(m));
    goto branch_969a;
branch_968c:;
    computed::push(m, m.ax());
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), 0x8));
    m.wr8(m.ss(), uint16_t(kActionTarget), uint8_t(m.al()));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x1));
    {
        const uint16_t callback_cs = m.cs();
        load_target_member_palette_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(computed::pop(m));
branch_969a:;
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), 0x1));
    (void)computed::subtract<uint16_t>(m, m.ax(), 0xC);
    if (!(m.flags() & emu::ZF))
        goto branch_965d;
    m.cx() = uint16_t(0x4);
branch_96a5:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x4);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        palette_step_toward_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_96a5;
    m.ax() = uint16_t(0x43);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = kGameState;
        play_sound_effect_entry_core(m);
        if (m.cs() != kGameState)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x10);
branch_96cf:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x4);
    m.cx() = uint16_t(0x3);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        palette_step_toward_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x8);
    m.cx() = uint16_t(0x4);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        palette_step_toward_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_96cf;
    m.cx() = uint16_t(0x19);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0x2));
    restore_formation_portrait_palette_core(m);
    m.set_al(uint8_t(0x1));
    restore_formation_portrait_palette_core(m);
    m.set_al(uint8_t(0x0));
    restore_formation_portrait_palette_core(m);
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x0));
    m.set_al(uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kActionTarget), uint8_t(m.al()));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0002))));
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x3));
    if (!(m.flags() & emu::ZF))
        goto branch_9735;
    computed::push(m, m.dx());
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x0));
    {
        const uint16_t callback_cs = m.cs();
        load_target_member_palette_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x1));
    {
        const uint16_t callback_cs = m.cs();
        load_target_member_palette_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.dx() = uint16_t(computed::pop(m));
branch_9735:;
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(kActionTarget), uint8_t(m.al()));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0002))));
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x3));
    if (!(m.flags() & emu::ZF))
        goto branch_9752;
    computed::push(m, m.dx());
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x0));
    {
        const uint16_t callback_cs = m.cs();
        load_target_member_palette_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x1));
    {
        const uint16_t callback_cs = m.cs();
        load_target_member_palette_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.dx() = uint16_t(computed::pop(m));
branch_9752:;
    m.set_al(uint8_t(0x2));
    m.wr8(m.ss(), uint16_t(kActionTarget), uint8_t(m.al()));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0002))));
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x3));
    if (!(m.flags() & emu::ZF))
        goto branch_976f;
    computed::push(m, m.dx());
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x0));
    {
        const uint16_t callback_cs = m.cs();
        load_target_member_palette_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x1));
    {
        const uint16_t callback_cs = m.cs();
        load_target_member_palette_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.dx() = uint16_t(computed::pop(m));
branch_976f:;
    m.set_al(uint8_t(0x3));
    m.wr8(m.ss(), uint16_t(kActionTarget), uint8_t(m.al()));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0002))));
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x3));
    if (!(m.flags() & emu::ZF))
        goto branch_978c;
    computed::push(m, m.dx());
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x0));
    {
        const uint16_t callback_cs = m.cs();
        load_target_member_palette_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x1));
    {
        const uint16_t callback_cs = m.cs();
        load_target_member_palette_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.dx() = uint16_t(computed::pop(m));
branch_978c:;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x3));
    m.set_al(uint8_t(0xFF));
    m.wr8(m.ss(), uint16_t(kEffectTargetMode), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_life_drain_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
branch_97ac:;
    m.wr8(m.ss(), uint16_t(kItemUseCount), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kGroupDrainKnockoutCount), uint8_t(0x0));
    m.set_al(uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kGroupDrainTargetCount), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x4);
    lookup_table_byte_core(m);
    m.wr8(m.ss(), uint16_t(kBattleItemPower), uint8_t(m.al()));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kGroupDrainPreviousGroup), uint8_t(0xFF));
    // Visit actors 0..30 except the caster and dead/stoned actors; each eligible actor takes item field 4.
branch_97d6:;
    computed::push(m, m.cx());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.cx()));
    if (!(m.flags() & emu::ZF))
        goto branch_97e2;
    goto branch_98a6;
branch_97e2:;
    m.set_al(uint8_t(uint8_t(m.cx())));
    m.set_al(uint8_t(uint8_t(m.cx())));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0002))));
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x3));
    if (m.flags() & emu::ZF)
        goto branch_97f4;
    goto branch_98a6;
branch_97f4:;
    m.wr8(m.ss(), uint16_t(kActionTarget), uint8_t(uint8_t(m.cx())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x001F))));
    party_slot_for_name_id_core(m);
    m.wr8(m.ss(), uint16_t(kTargetGroup), uint8_t(m.al()));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kGroupDrainPreviousGroup))));
    (void)computed::subtract<uint8_t>(m, uint8_t(m.dx()), m.al());
    if (m.flags() & emu::ZF)
        goto branch_9855;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x5);
    if (m.flags() & emu::CF)
        goto branch_9841;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kGroupDrainKnockoutCount))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_9841;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kTargetGroup))));
    m.wr8(m.ss(), uint16_t(kGroupDrainSavedTarget), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kGroupDrainPreviousGroup))));
    m.wr8(m.ss(), uint16_t(kTargetGroup), uint8_t(m.al()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kGroupDrainKnockoutCount))));
    m.wr16(m.ss(), uint16_t(kMeterInputTarget), uint16_t(m.ax()));
    show_damage_message_call_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kGroupDrainSavedTarget))));
    m.wr8(m.ss(), uint16_t(kTargetGroup), uint8_t(m.al()));
branch_9841:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kTargetGroup))));
    m.wr8(m.ss(), uint16_t(kGroupDrainPreviousGroup), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kItemUseCount), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kGroupDrainKnockoutCount), uint8_t(0x0));
branch_9855:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kGroupDrainTargetCount))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x1)));
    m.wr8(m.ss(), uint16_t(kGroupDrainTargetCount), uint8_t(m.al()));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kBattleItemPower))));
    m.dx() = uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(0x0)) << 8));
    m.wr16(m.ss(), uint16_t(kMeterInputTarget), uint16_t(m.dx()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kGroupDrainTargetCount))));
    {
        const uint16_t callback_cs = m.cs();
        show_drain_damage_to_target_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.wr8(m.ss(), uint16_t(kBattleKnockoutMode), uint8_t(0x0));
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_9885;
    m.wr8(m.ss(), uint16_t(kBattleKnockoutMode), uint8_t(0x1));
    goto branch_988f;
branch_9885:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kItemUseCount))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x1)));
    m.wr8(m.ss(), uint16_t(kItemUseCount), uint8_t(m.al()));
branch_988f:;
    set_battle_meter_target_core(m);
    check_target_knocked_out_core(m);
    if (!(m.flags() & emu::CF))
        goto branch_98a6;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x4);
    if (m.flags() & emu::CF)
        goto branch_98a6;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kGroupDrainKnockoutCount))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x1)));
    m.wr8(m.ss(), uint16_t(kGroupDrainKnockoutCount), uint8_t(m.al()));
branch_98a6:;
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx()));
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x1F);
    if (m.flags() & emu::ZF)
        goto branch_98b0;
    goto branch_97d6;
    // Heal the caster by eligible target count * power, capped at max HP; overkill still counts full power.
branch_98b0:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    m.wr8(m.ss(), uint16_t(kBattleImmunityMode), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    m.wr8(m.ss(), uint16_t(kActionTarget), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x2));
    m.set_al(uint8_t(0xFF));
    m.wr8(m.ss(), uint16_t(kEffectTargetMode), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_healing_glow_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleImmunityMode))));
    m.wr8(m.ss(), uint16_t(kActionTarget), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kGroupDrainTargetCount))));
    m.set_ah(uint8_t(m.rd8(m.ss(), uint16_t(kBattleItemPower))));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.dx() = uint16_t(m.ax());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    battle_actor_pointer_core(m);
    m.bx() = uint16_t(m.rd16(m.ss(), uint16_t(m.si() + 0x0003)));
    m.cx() = uint16_t(m.rd16(m.ss(), uint16_t(m.si() + 0x0005)));
    m.ax() = uint16_t(m.bx());
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), m.dx()));
    (void)computed::subtract<uint16_t>(m, m.bx(), m.cx());
    if ((m.flags() & emu::ZF) || (bool(m.flags() & emu::SF) != bool(m.flags() & emu::OF)))
        goto branch_98ff;
    m.bx() = uint16_t(m.cx());
branch_98ff:;
    BattleActorState(m, m.si()).write_hp(uint16_t(m.bx()));
    m.bx() = uint16_t(computed::subtract<uint16_t>(m, m.bx(), m.ax()));
    m.wr16(m.ss(), uint16_t(kMeterInputTarget), uint16_t(m.dx()));
    {
        const uint16_t callback_cs = m.cs();
        show_hp_drained_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0xF));
    draw_battle_skill_list_core(m);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
branch_991a:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kGroupDrainKnockoutCount))));
    m.wr8(m.ss(), uint16_t(kBattleStatusSuccessCount), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_group_drain_action(Machine &m) {
    execute_group_drain_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x369931 (near)
// Add the effect's power to target defense and show the increase.
void execute_defense_increase_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_995b;
    {
        const uint16_t callback_cs = m.cs();
        announce_used_battle_item_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() ^ m.al()))));
    m.wr8(m.ss(), uint16_t(kTextColorScratch), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_995b;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x3));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_defense_increase_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_995b;
    // Defense gains item field 4 with byte wrapping; there is no 99 or 255 cap.
branch_995b:;
    {
        const uint16_t callback_cs = m.cs();
        read_target_defense_and_effect_power_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), uint8_t(m.bx()))));
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    m.bx() = uint16_t(
        (m.bx() & 0x00FF) |
        (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx() >> 8) ^ uint8_t(m.bx() >> 8)))))
         << 8));
    m.wr16(m.ss(), uint16_t(kMeterInputTarget), uint16_t(m.bx()));
    {
        const uint16_t callback_cs = m.cs();
        show_target_status_defense_increased_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_defense_increase_action(Machine &m) {
    execute_defense_increase_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x369972 (near)
// Replace target strength with the effect-table value and show the increase message.
void execute_strength_replacement_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_99d0;
    {
        const uint16_t callback_cs = m.cs();
        announce_used_battle_item_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_99a9;
    selected_battle_equipment_call_core(m);
    (void)computed::subtract<uint8_t>(m, m.al(), 0xF4);
    if (m.flags() & emu::ZF)
        goto branch_9994;
    goto branch_99a9;
branch_9994:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    m.wr8(m.ss(), uint16_t(kActionTarget), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x3));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_protective_aura_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_99a9;
branch_99a9:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x4);
    lookup_table_byte_core(m);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0007))));
    BattleActorState(m, m.si()).write_stat(7, uint8_t(m.cx()));
    m.cx() = uint16_t(
        (m.cx() & 0x00FF) |
        (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.cx() >> 8) ^ uint8_t(m.cx() >> 8)))))
         << 8));
    m.wr16(m.ss(), uint16_t(kMeterInputTarget), uint16_t(m.cx()));
    {
        const uint16_t callback_cs = m.cs();
        show_target_strength_increased_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_99d0:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_strength_replacement_action(Machine &m) {
    execute_strength_replacement_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x3699DF (near)
// Replace the actor's defense with the effect-table value and show the increase message.
void execute_defense_replacement_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_9a45;
    {
        const uint16_t callback_cs = m.cs();
        announce_used_battle_item_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_9a16;
    selected_battle_equipment_call_core(m);
    (void)computed::subtract<uint8_t>(m, m.al(), 0xF5);
    if (m.flags() & emu::ZF)
        goto branch_9a01;
    goto branch_9a16;
branch_9a01:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    m.wr8(m.ss(), uint16_t(kActionTarget), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x2));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_protective_aura_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_9a16;
branch_9a16:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    m.wr8(m.ss(), uint16_t(kActionTarget), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x4);
    lookup_table_byte_core(m);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0008))));
    BattleActorState(m, m.si()).write_stat(8, uint8_t(m.cx()));
    m.cx() = uint16_t(
        (m.cx() & 0x00FF) |
        (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.cx() >> 8) ^ uint8_t(m.cx() >> 8)))))
         << 8));
    m.wr16(m.ss(), uint16_t(kMeterInputTarget), uint16_t(m.cx()));
    {
        const uint16_t callback_cs = m.cs();
        show_target_defense_increased_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_9a45:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_defense_replacement_action(Machine &m) {
    execute_defense_replacement_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x369A54 (near)
// Apply power plus random spread minus effective defense, retaining signed damage through resistance.
void execute_unanimated_fixed_damage_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_9a63;
    {
        const uint16_t callback_cs = m.cs();
        announce_used_battle_item_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    // Original quirk: negative power + spread - defense is passed through as signed meter input, which can
    // heal.
branch_9a63:;
    {
        const uint16_t callback_cs = m.cs();
        read_target_effective_defense_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        roll_effect_power_and_spread_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(uint8_t(m.cx())));
    m.dx() = uint16_t(computed::subtract<uint16_t>(m, m.dx(), m.ax()));
    {
        const uint16_t callback_cs = m.cs();
        halve_damage_for_effect_resistance_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    set_battle_meter_target_core(m);
    open_party_member_popup_core(m);
    check_target_knocked_out_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_unanimated_fixed_damage_action(Machine &m) {
    execute_unanimated_fixed_damage_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x369A8A (near)
// Apply magic-scaled consumption damage and display the consumed-target message.
void execute_consume_target_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_9ad2;
    announce_battle_action_call_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_9ad2;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_9ad2;
    selected_battle_equipment_call_core(m);
    (void)computed::subtract<uint8_t>(m, m.al(), 0xE4);
    if (m.flags() & emu::ZF)
        goto branch_9ab8;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xE9);
    if (m.flags() & emu::ZF)
        goto branch_9ac5;
    goto branch_9ad2;
branch_9ab8:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x3));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_battle_effect_5_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_9ad2;
branch_9ac5:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_liquid_jet_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_9ad2;
branch_9ad2:;
    {
        const uint16_t callback_cs = m.cs();
        compute_magic_consumption_damage_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    set_battle_meter_target_core(m);
    {
        const uint16_t callback_cs = m.cs();
        show_target_consumed_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    check_target_knocked_out_core(m);
    if (m.flags() & emu::CF)
        goto branch_9b09;
    has_table_flag_bit1_core(m);
    if (m.flags() & emu::CF)
        goto branch_9ae7;
    goto branch_9b09;
branch_9ae7:;
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x2));
    if (!(m.flags() & emu::ZF))
        goto branch_9af5;
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x4));
    if (!(m.flags() & emu::ZF))
        goto branch_9aff;
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x10));
    if (!(m.flags() & emu::ZF))
        goto branch_9b04;
    goto branch_9b09;
branch_9af5:;
    resolve_drain_hit_call_core(m);
    goto branch_9b09;
branch_9aff:;
    resolve_status_hit_call_core(m);
    goto branch_9b09;
branch_9b04:;
    resolve_fixed_damage_hit_call_core(m);
    goto branch_9b09;
branch_9b09:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_consume_target_action(Machine &m) {
    execute_consume_target_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x369B18 (near)
// Announce the action, wait sixty frames and show that nothing happened.
void execute_no_effect_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_9b3a;
    announce_battle_action_call_core(m);
    m.ax() = uint16_t(0x1);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = kGameState;
        play_sound_effect_entry_core(m);
        if (m.cs() != kGameState)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t(0x3C);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    show_no_effect_message_call_core(m);
branch_9b3a:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_no_effect_action(Machine &m) {
    execute_no_effect_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x369B49 (near)
// Add effect power to the actor's agility and announce its duplication effect.
void execute_agility_increase_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_9b9b;
    {
        const uint16_t callback_cs = m.cs();
        announce_used_battle_item_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_9b73;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    m.wr8(m.ss(), uint16_t(kActionTarget), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_protective_aura_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    // Agility gains item field 4 with byte wrapping; strength/defense replacement actions instead assign it.
branch_9b73:;
    computed::push(m, m.bx());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x4);
    lookup_table_byte_core(m);
    m.bx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    m.wr8(m.ss(), uint16_t(kActionTarget), uint8_t(m.al()));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0009))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), uint8_t(m.cx()))));
    m.wr8(m.ss(), uint16_t(m.si() + 0x0009), uint8_t(m.al()));
    {
        const uint16_t callback_cs = m.cs();
        show_actor_duplicated_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_9b9b:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_agility_increase_action(Machine &m) {
    execute_agility_increase_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x369BAA (near)
// Mark the actor as departed, using the forced-removal path when fleeing is forbidden.
void execute_flee_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlags))));
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x1));
    if (!(m.flags() & emu::ZF))
        goto branch_9bea;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    m.wr8(m.ss(), uint16_t(kActionTarget), uint8_t(m.al()));
    {
        const uint16_t callback_cs = m.cs();
        show_actor_fled_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0002))));
    m.set_al(uint8_t(0x1));
    BattleActorState(m, m.si()).write_status(uint8_t(m.al()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.si() + 0x0003)));
    m.ax() = uint16_t(0x0);
    BattleActorState(m, m.si()).write_hp(uint16_t(m.ax()));
    m.set_al(uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kBattleKnockoutMode), uint8_t(m.al()));
    check_target_knocked_out_core(m);
    goto branch_9c28;
branch_9bea:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    m.wr8(m.ss(), uint16_t(kActionTarget), uint8_t(m.al()));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x001F))));
    computed::push(m, m.si());
    party_slot_for_name_id_core(m);
    m.wr8(m.ss(), uint16_t(kTargetGroup), uint8_t(m.al()));
    m.si() = uint16_t(computed::pop(m));
    m.set_al(uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kActingActor), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0002))));
    m.set_al(uint8_t(0x1));
    BattleActorState(m, m.si()).write_status(uint8_t(m.al()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.si() + 0x0003)));
    m.ax() = uint16_t(0x0);
    BattleActorState(m, m.si()).write_hp(uint16_t(m.ax()));
    m.wr8(m.ss(), uint16_t(kBattleKnockoutMode), uint8_t(0x1));
    check_target_knocked_out_core(m);
branch_9c28:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_flee_action(Machine &m) {
    execute_flee_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x369C37 (near)
// Apply innate status effects after element-immunity and magic-based success checks.
void execute_immunity_checked_status_action_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_9c9a;
    {
        const uint16_t callback_cs = m.cs();
        announce_used_battle_item_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_9c9a;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_9c9a;
    selected_battle_equipment_call_core(m);
    (void)computed::subtract<uint8_t>(m, m.al(), 0xAA);
    if (m.flags() & emu::ZF)
        goto branch_9c69;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xAB);
    if (m.flags() & emu::ZF)
        goto branch_9c80;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xE9);
    if (m.flags() & emu::ZF)
        goto branch_9c8d;
    goto branch_9c9a;
branch_9c69:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_riddle_opening_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_riddle_flash_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_riddle_fade_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_9c9a;
branch_9c80:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_song_animation_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_9c9a;
branch_9c8d:;
    m.wr8(m.ss(), uint16_t(kEffectPaletteVariant), uint8_t(0x4));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x7000;
        play_battle_effect_5_core(m);
        if (m.cs() != 0x7000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_9c9a;
branch_9c9a:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x5);
    lookup_table_byte_core(m);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_stat_byte_b_call_core(m);
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & uint8_t(m.cx())))));
    if (m.flags() & emu::ZF)
        goto branch_9cb9;
    {
        const uint16_t callback_cs = m.cs();
        show_element_resistance_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    goto branch_9d7f;
branch_9cb9:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    {
        const uint16_t callback_cs = m.cs();
        read_target_element_resistances_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & uint8_t(m.cx())))));
    if (!(m.flags() & emu::ZF))
        goto branch_9ce6;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x000A))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x20)));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x000A))));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(uint8_t(m.cx())));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), uint8_t(m.dx()))));
    if (!(m.flags() & emu::CF))
        goto branch_9ce8;
branch_9ce6:;
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() ^ m.al()))));
    // Succeed when a 0..100 roll >= max(byte(target magic + 32) - caster magic, 0); mask matches force zero.
branch_9ce8:;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0x6400);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.cx()));
    if (!(m.flags() & emu::CF))
        goto branch_9cfc;
    show_no_effect_message_call_core(m);
    goto branch_9d7f;
branch_9cfc:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.bx() = uint16_t(0x4);
    lookup_table_byte_core(m);
    m.dx() = uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0002))));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.al()));
    (void)computed::logic<uint8_t>(m, uint8_t(uint8_t(m.dx() >> 8) & uint8_t(m.dx())));
    if (!(m.flags() & emu::ZF))
        goto branch_9d28;
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | uint8_t(m.dx() >> 8)))));
    if (!BattleActorState(m, m.si()).write_status(uint8_t(m.al()))) {
        goto branch_9d28;
    }
    m.set_al(uint8_t(uint8_t(m.dx() >> 8)));
    show_status_inflicted_message_core(m);
    goto branch_9d2d;
branch_9d28:;
    show_no_effect_message_call_core(m);
    goto branch_9d7f;
    // Original quirk: both tests use bit 0, so petrification alone does not increment the success count here.
branch_9d2d:;
    (void)computed::logic<uint8_t>(m, uint8_t(uint8_t(m.dx() >> 8) & 0x1));
    if (m.flags() & emu::ZF)
        goto branch_9d48;
    (void)computed::logic<uint8_t>(m, uint8_t(uint8_t(m.dx() >> 8) & 0x1));
    if (m.flags() & emu::ZF)
        goto branch_9d3e;
    m.ax() = uint16_t(0x0);
    BattleActorState(m, m.si()).write_hp(uint16_t(m.ax()));
branch_9d3e:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleStatusSuccessCount))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x1)));
    m.wr8(m.ss(), uint16_t(kBattleStatusSuccessCount), uint8_t(m.al()));
branch_9d48:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_9d55;
    remove_defeated_enemy_group_core(m);
    goto branch_9d58;
branch_9d55:;
    shake_screen_for_party_hit_core(m);
branch_9d58:;
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0xF));
    draw_battle_skill_list_core(m);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    draw_battle_member_panels_core(m);
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x6), m.al());
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_sprite_table_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 0x0009);
branch_9d7f:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleFlashCounter))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kBattleFlashCounter), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void execute_immunity_checked_status_action(Machine &m) {
    execute_immunity_checked_status_action_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x369E02 (near)
// Build the target actor's status-icon token list.
void build_target_status_icons_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    computed::push(m, m.di());
    m.di() = uint16_t(kBattleTargetIcons);
    build_status_icons_core(m);
    m.di() = uint16_t(computed::pop(m));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void build_target_status_icons(Machine &m) {
    build_target_status_icons_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x369FBD (near)
// Display the target's resistance to the current action's element or effect.
void show_element_resistance_message_core(Machine &m) {
    JoinedBattlePopup joined(m, "show_element_resistance_message");
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    build_battle_member_display_core(m);
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_ah(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x1));
    build_icon_list_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleTargetIcons);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_element_resistance_message_1);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_element_resistance_message_1);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleItemIcons);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_element_resistance_message_2);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_element_resistance_message_2);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_element_resistance_message_3);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_element_resistance_message_3);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }

    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_element_resistance_message(Machine &m) {
    show_element_resistance_message_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36A18B (near)
// Show the shattering message for the current weapon.
void show_weapon_shattered_message_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    m.set_ah(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x1));
    build_icon_list_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleItemIcons);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_weapon_shattered_message_1);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_weapon_shattered_message_1);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }

    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_weapon_shattered_message(Machine &m) {
    show_weapon_shattered_message_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36A1DC (near)
// Display the actor and item or ability used for the action.
void announce_used_battle_item_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    build_battle_member_name_or_icons_core(m);
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    m.set_ah(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x1));
    build_icon_list_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleActorIcons);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::announce_used_battle_item_1);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::announce_used_battle_item_1);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleItemIcons);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::announce_used_battle_item_2);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::announce_used_battle_item_2);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void announce_used_battle_item(Machine &m) {
    announce_used_battle_item_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36A253 (near)
// Display the actor's innate attack and any special-body qualification.
void announce_innate_battle_attack_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    build_battle_member_name_or_icons_core(m);
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    m.set_ah(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x1));
    build_icon_list_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleActorIcons);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::announce_innate_battle_attack_1);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::announce_innate_battle_attack_1);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleItemIcons);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::announce_innate_battle_attack_2);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::announce_innate_battle_attack_2);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void announce_innate_battle_attack(Machine &m) {
    announce_innate_battle_attack_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36A341 (near)
// Show that the self-destruction was futile.
void show_futile_self_destruct_message_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    {
        const TextPtr message_text = text(m, TextKey::show_futile_self_destruct_message_1);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_futile_self_destruct_message_1);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }

    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_futile_self_destruct_message(Machine &m) {
    show_futile_self_destruct_message_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36A4F7 (near)
// Display the damage amount applied to all enemies.
void show_group_damage_message_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    {
        const TextPtr message_text = text(m, TextKey::show_group_damage_message_1);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_group_damage_message_1);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kMeterInputTarget)));
    format_number_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleNumberText);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_group_damage_message_2);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_group_damage_message_2);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }

    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_group_damage_message(Machine &m) {
    show_group_damage_message_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36A5BD (near)
// Display a target's damage amount during a drain action.
void show_drain_damage_to_target_message_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActionTarget))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::CF))
        goto branch_a5f4;
    build_action_target_description_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleTargetIcons);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_drain_damage_to_target_message_1);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_drain_damage_to_target_message_1);
    goto branch_a60d;
branch_a5f4:;
    build_battle_member_display_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleTargetIcons);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    queue_item_count_footer_call_core(m);
branch_a60d:;
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kMeterInputTarget)));
    format_number_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleNumberText);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_drain_damage_to_target_message_2);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_drain_damage_to_target_message_2);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }

    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_drain_damage_to_target_message(Machine &m) {
    show_drain_damage_to_target_message_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36A6CC (near)
// Display that the target was cut apart.
void show_target_dismembered_message_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    build_battle_member_display_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleTargetIcons);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_target_dismembered_message_1);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_dismembered_message_1);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }

    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_target_dismembered_message(Machine &m) {
    show_target_dismembered_message_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36A70E (near)
// Display the amount of HP absorbed from the target.
void show_hp_drained_message_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    build_battle_member_display_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kMeterInputTarget)));
    format_number_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleNumberText);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_hp_drained_message_1);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_hp_drained_message_1);
    {
        const TextPtr message_text = text(m, TextKey::show_hp_drained_message_2);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_hp_drained_message_2);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }

    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_hp_drained_message(Machine &m) {
    show_hp_drained_message_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36A771 (near)
// Display the amount of HP digested from the target.
void show_target_consumed_message_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    build_battle_member_display_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kMeterInputTarget)));
    format_number_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleNumberText);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_target_consumed_message_1);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_consumed_message_1);
    {
        const TextPtr message_text = text(m, TextKey::show_target_consumed_message_2);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_consumed_message_2);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }

    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_target_consumed_message(Machine &m) {
    show_target_consumed_message_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36A7D4 (near)
// Display the target's recovered HP amount.
void show_target_hp_recovery_message_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    build_battle_member_display_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kMeterInputTarget)));
    format_number_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleTargetIcons);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_target_hp_recovery_message_1);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_hp_recovery_message_1);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleNumberText);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_target_hp_recovery_message_2);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_hp_recovery_message_2);
    {
        const TextPtr message_text = text(m, TextKey::show_target_hp_recovery_message_3);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_hp_recovery_message_3);
    {
        const TextPtr message_text = text(m, TextKey::show_target_hp_recovery_message_4);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_hp_recovery_message_4);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }

    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_target_hp_recovery_message(Machine &m) {
    show_target_hp_recovery_message_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36AB5A (near)
// Display that the acting actor fled.
void show_actor_fled_message_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    build_battle_member_name_or_icons_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleActorIcons);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_actor_fled_message_1);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_actor_fled_message_1);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }

    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_actor_fled_message(Machine &m) {
    show_actor_fled_message_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36ACBF (near)
// Display the critical-hit announcement.
void show_critical_hit_message_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    {
        const TextPtr message_text = text(m, TextKey::show_critical_hit_message_1);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_critical_hit_message_1);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }

    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_critical_hit_message(Machine &m) {
    show_critical_hit_message_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36ACEB (near)
// Display the actor guarding with the current equipment or ability.
void announce_battle_defense_core(Machine &m) {
    JoinedBattlePopup joined(m, "announce_battle_defense");
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    build_battle_member_name_or_icons_core(m);
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kActingActor))));
    m.set_ah(uint8_t(m.rd8(m.ss(), uint16_t(kActionId))));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x1));
    build_icon_list_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleActorIcons);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::announce_battle_defense_1);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::announce_battle_defense_1);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleItemIcons);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::announce_battle_defense_2);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::announce_battle_defense_2);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }

    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void announce_battle_defense(Machine &m) {
    announce_battle_defense_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36AD72 (near)
// Display that a barrier covered the party.
void show_barrier_formed_message_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    {
        const TextPtr message_text = text(m, TextKey::show_barrier_formed_message_1);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_barrier_formed_message_1);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }

    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_barrier_formed_message(Machine &m) {
    show_barrier_formed_message_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36AD9E (near)
// Display the target's defense increase amount.
void show_target_defense_increased_message_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    build_battle_member_display_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kMeterInputTarget)));
    format_number_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleTargetIcons);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_target_defense_increased_message_1);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_defense_increased_message_1);
    {
        const TextPtr message_text = text(m, TextKey::show_target_defense_increased_message_2);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_defense_increased_message_2);
    {
        const TextPtr message_text = text(m, TextKey::show_target_defense_increased_message_3);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_defense_increased_message_3);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleNumberText);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_target_defense_increased_message_4);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_defense_increased_message_4);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }

    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_target_defense_increased_message(Machine &m) {
    show_target_defense_increased_message_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36AE3A (near)
// Display the defense increase using the target's status-icon description.
void show_target_status_defense_increased_message_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    {
        const uint16_t callback_cs = m.cs();
        build_target_status_icons_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kMeterInputTarget)));
    format_number_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleTargetIcons);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_target_status_defense_increased_message_1);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_status_defense_increased_message_1);
    {
        const TextPtr message_text = text(m, TextKey::show_target_status_defense_increased_message_2);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_status_defense_increased_message_2);
    {
        const TextPtr message_text = text(m, TextKey::show_target_status_defense_increased_message_3);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_status_defense_increased_message_3);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleNumberText);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_target_status_defense_increased_message_4);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_status_defense_increased_message_4);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }

    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_target_status_defense_increased_message(Machine &m) {
    show_target_status_defense_increased_message_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36AED6 (near)
// Display the target's strength increase amount.
void show_target_strength_increased_message_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    build_action_target_description_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kMeterInputTarget)));
    format_number_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleTargetIcons);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_target_strength_increased_message_1);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_strength_increased_message_1);
    {
        const TextPtr message_text = text(m, TextKey::show_target_strength_increased_message_2);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_strength_increased_message_2);
    {
        const TextPtr message_text = text(m, TextKey::show_target_strength_increased_message_3);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_strength_increased_message_3);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleNumberText);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_target_strength_increased_message_4);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_strength_increased_message_4);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }

    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_target_strength_increased_message(Machine &m) {
    show_target_strength_increased_message_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36AF72 (near)
// Display that the actor is waiting to counterattack, or announce a reflected attack.
void show_counter_stance_message_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    build_battle_member_name_or_icons_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleActorIcons);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_counter_stance_message_1);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_counter_stance_message_1);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }

    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_counter_stance_message(Machine &m) {
    show_counter_stance_message_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36B17B (near)
// Display that the actor self-destructed.
void show_actor_self_destructed_message_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    build_battle_member_name_or_icons_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleActorIcons);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_actor_self_destructed_message_1);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_actor_self_destructed_message_1);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_actor_self_destructed_message(Machine &m) {
    show_actor_self_destructed_message_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36B1E6 (near)
// Display that the whip wrapped around the target.
void show_whip_entangled_message_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    build_battle_member_name_or_icons_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleActorIcons);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_whip_entangled_message_1);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_whip_entangled_message_1);
    {
        const TextPtr message_text = text(m, TextKey::show_whip_entangled_message_2);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_whip_entangled_message_2);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_whip_entangled_message(Machine &m) {
    show_whip_entangled_message_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36B238 (near)
// Display that the target woke up.
void show_target_awakened_message_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    build_battle_member_display_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleTargetIcons);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_target_awakened_message_1);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_awakened_message_1);
    {
        const TextPtr message_text = text(m, TextKey::show_target_awakened_message_2);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_awakened_message_2);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }

    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_target_awakened_message(Machine &m) {
    show_target_awakened_message_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36B28D (near)
// Display that the target's paralysis ended.
void show_target_paralysis_ended_message_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    build_battle_member_display_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleTargetIcons);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_target_paralysis_ended_message_1);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_paralysis_ended_message_1);
    {
        const TextPtr message_text = text(m, TextKey::show_target_paralysis_ended_message_2);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_paralysis_ended_message_2);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }

    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_target_paralysis_ended_message(Machine &m) {
    show_target_paralysis_ended_message_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36B2E2 (near)
// Display that the target's poison ended.
void show_target_poison_ended_message_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    build_battle_member_display_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleTargetIcons);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_target_poison_ended_message_1);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_poison_ended_message_1);
    {
        const TextPtr message_text = text(m, TextKey::show_target_poison_ended_message_2);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_poison_ended_message_2);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }

    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_target_poison_ended_message(Machine &m) {
    show_target_poison_ended_message_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36B38C (near)
// Display that the target's curse ended.
void show_target_curse_ended_message_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    build_battle_member_display_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleTargetIcons);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_target_curse_ended_message_1);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_curse_ended_message_1);
    {
        const TextPtr message_text = text(m, TextKey::show_target_curse_ended_message_2);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_curse_ended_message_2);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }

    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_target_curse_ended_message(Machine &m) {
    show_target_curse_ended_message_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36B3E1 (near)
// Display that the target can see again.
void show_target_sight_restored_message_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    build_battle_member_display_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleTargetIcons);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_target_sight_restored_message_1);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_sight_restored_message_1);
    {
        const TextPtr message_text = text(m, TextKey::show_target_sight_restored_message_2);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_sight_restored_message_2);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }

    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_target_sight_restored_message(Machine &m) {
    show_target_sight_restored_message_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36B436 (near)
// Display that the target returned to its senses.
void show_target_sanity_restored_message_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    build_battle_member_display_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleTargetIcons);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_target_sanity_restored_message_1);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_sanity_restored_message_1);
    {
        const TextPtr message_text = text(m, TextKey::show_target_sanity_restored_message_2);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_sanity_restored_message_2);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }

    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_target_sanity_restored_message(Machine &m) {
    show_target_sanity_restored_message_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36B750 (near)
// Display that the scripture has no effect on the target.
void show_scripture_ineffective_message_core(Machine &m) {
    JoinedBattlePopup joined(m, "show_scripture_ineffective_message");
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    build_battle_member_display_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleTargetIcons);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    append_target_group_suffix_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFooterPanelMode))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_b777;
    goto branch_b777;
branch_b777:;
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_scripture_ineffective_message_1);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_scripture_ineffective_message_1);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }

    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_scripture_ineffective_message(Machine &m) {
    show_scripture_ineffective_message_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36B7AC (near)
// Display that the actor created a duplicate.
void show_actor_duplicated_message_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    build_battle_member_name_or_icons_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleActorIcons);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_actor_duplicated_message_1);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_actor_duplicated_message_1);
    {
        const TextPtr message_text = text(m, TextKey::show_actor_duplicated_message_2);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_actor_duplicated_message_2);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }

    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_actor_duplicated_message(Machine &m) {
    show_actor_duplicated_message_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36B801 (near)
// Display that the actor's HP returned to normal.
void show_actor_hp_restored_message_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    build_battle_member_name_or_icons_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleActorIcons);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_actor_hp_restored_message_1);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_actor_hp_restored_message_1);
    {
        const TextPtr message_text = text(m, TextKey::show_actor_hp_restored_message_2);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_actor_hp_restored_message_2);
    {
        const TextPtr message_text = text(m, TextKey::show_actor_hp_restored_message_3);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_actor_hp_restored_message_3);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }

    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_actor_hp_restored_message(Machine &m) {
    show_actor_hp_restored_message_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36B869 (near)
// Display the amount by which the target's strength decreased.
void show_target_strength_decreased_message_core(Machine &m) {
    JoinedBattlePopup joined(m, "show_target_strength_decreased_message");
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    build_battle_member_display_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleTargetIcons);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    append_target_group_suffix_core(m);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kMeterInputTarget)));
    format_number_core(m);
    {
        const TextPtr message_text = text(m, TextKey::show_target_strength_decreased_message_1);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_strength_decreased_message_1);
    {
        const TextPtr message_text = text(m, TextKey::show_target_strength_decreased_message_2);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_strength_decreased_message_2);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleNumberText);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_target_strength_decreased_message_3);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_strength_decreased_message_3);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }

    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_target_strength_decreased_message(Machine &m) {
    show_target_strength_decreased_message_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36B902 (near)
// Display the amount by which the target's agility decreased.
void show_target_agility_decreased_message_core(Machine &m) {
    JoinedBattlePopup joined(m, "show_target_agility_decreased_message");
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    build_battle_member_display_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleTargetIcons);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    append_target_group_suffix_core(m);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kMeterInputTarget)));
    format_number_core(m);
    {
        const TextPtr message_text = text(m, TextKey::show_target_agility_decreased_message_1);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_agility_decreased_message_1);
    {
        const TextPtr message_text = text(m, TextKey::show_target_agility_decreased_message_2);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_agility_decreased_message_2);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleNumberText);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_target_agility_decreased_message_3);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_agility_decreased_message_3);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }

    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_target_agility_decreased_message(Machine &m) {
    show_target_agility_decreased_message_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36B99B (near)
// Display the target's magic or defense reduction amount according to the selected stat.
void show_target_magic_or_defense_decreased_message_core(Machine &m) {
    JoinedBattlePopup joined(m, "show_target_magic_or_defense_decreased_message");
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    build_battle_member_display_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleTargetIcons);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    append_target_group_suffix_core(m);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kMeterInputTarget)));
    format_number_core(m);
    {
        const TextPtr message_text = text(m, TextKey::show_target_magic_or_defense_decreased_message_1);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_magic_or_defense_decreased_message_1);
    {
        const TextPtr message_text = text(m, TextKey::show_target_magic_or_defense_decreased_message_2);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_magic_or_defense_decreased_message_2);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleNumberText);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_target_magic_or_defense_decreased_message_3);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_target_magic_or_defense_decreased_message_3);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }

    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_target_magic_or_defense_decreased_message(Machine &m) {
    show_target_magic_or_defense_decreased_message_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36BACD (near)
// Display the amount of gold stolen.
void show_gold_stolen_message_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBattleStatusSuccessCount))));
    format_number_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kMeterInputTarget)));
    format_number_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleNumberText);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_gold_stolen_message_1);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_gold_stolen_message_1);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }

    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_gold_stolen_message(Machine &m) {
    show_gold_stolen_message_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36BB2A (near)
// Display the actor's attempted theft and whether it failed.
void show_gold_theft_result_message_core(Machine &m) {
    JoinedBattlePopup joined(m, "show_gold_theft_result_message");
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    build_battle_member_name_or_icons_core(m);
    m.ax() = uint16_t(0x0);
    m.di() = uint16_t(kBattleTargetIcons);
    build_status_icons_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleActorIcons);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_gold_theft_result_message_1);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_gold_theft_result_message_1);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleTargetIcons);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_gold_theft_result_message_2);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_gold_theft_result_message_2);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_gold_theft_result_message_3);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_gold_theft_result_message_3);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }

    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_gold_theft_result_message(Machine &m) {
    show_gold_theft_result_message_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36BBF9 (near)
// Display the target's reflection message or game-over announcement.
void show_magic_reflected_or_game_over_message_core(Machine &m) {
    JoinedBattlePopup joined(m, "show_magic_reflected_or_game_over_message");
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    build_battle_member_display_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleTargetIcons);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_magic_reflected_or_game_over_message_1);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_magic_reflected_or_game_over_message_1);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_magic_reflected_or_game_over_message_2);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_magic_reflected_or_game_over_message_2);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }

    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_magic_reflected_or_game_over_message(Machine &m) {
    show_magic_reflected_or_game_over_message_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36BCCF (near)
// Dispatch the recovery message for the cleared status bit.
void show_cured_status_message_core(Machine &m) {
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x1));
    if (!(m.flags() & emu::ZF))
        goto branch_bcf1;
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x2));
    if (!(m.flags() & emu::ZF))
        goto branch_bcf3;
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x4));
    if (!(m.flags() & emu::ZF))
        goto branch_bcf5;
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x8));
    if (!(m.flags() & emu::ZF))
        goto branch_bcfa;
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x10));
    if (!(m.flags() & emu::ZF))
        goto branch_bcff;
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x20));
    if (!(m.flags() & emu::ZF))
        goto branch_bd04;
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x40));
    if (!(m.flags() & emu::ZF))
        goto branch_bd09;
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x80));
    if (!(m.flags() & emu::ZF))
        goto branch_bd0e;
    goto branch_bd13;
branch_bcf1:;
    goto branch_bd13;
branch_bcf3:;
    goto branch_bd13;
branch_bcf5:;
    {
        const uint16_t callback_cs = m.cs();
        show_target_curse_ended_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    goto branch_bd13;
branch_bcfa:;
    {
        const uint16_t callback_cs = m.cs();
        show_target_sight_restored_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    goto branch_bd13;
branch_bcff:;
    {
        const uint16_t callback_cs = m.cs();
        show_target_poison_ended_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    goto branch_bd13;
branch_bd04:;
    {
        const uint16_t callback_cs = m.cs();
        show_target_sanity_restored_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    goto branch_bd13;
branch_bd09:;
    {
        const uint16_t callback_cs = m.cs();
        show_target_paralysis_ended_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    goto branch_bd13;
branch_bd0e:;
    {
        const uint16_t callback_cs = m.cs();
        show_target_awakened_message_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    goto branch_bd13;
branch_bd13:;
    return;
}
void show_cured_status_message(Machine &m) {
    show_cured_status_message_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36C35C (near)
// Display the number of hits in the current attack.
void show_multi_hit_count_message_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kMeterInputTarget)));
    format_number_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kBattleNumberText);
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr message_text = text(m, TextKey::show_multi_hit_count_message_1);
        m.ax() = message_text.segment;
        m.ds() = message_text.segment;
        m.si() = message_text.offset;
    }
    m.ax() = uint16_t(0xFF);
    m.bx() = uint16_t(0xFF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_word_queue_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    text_done(m, TextKey::show_multi_hit_count_message_1);
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kIconQueue);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }

    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_multi_hit_count_message(Machine &m) {
    show_multi_hit_count_message_core(m);
    if (m.cs() == 0x6000)
        m.ret();
}
// rom: 0x36C804
// Load the front formation layer's tiles and palette and adjust its drawing origin.
void load_formation_front_layer_core(Machine &m) {
    m.ax() = uint16_t(0xE000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kFormationEffectTileSource)));
    m.si() = uint16_t(m.ax());
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kFormationEffectTileCount)));
    m.cx() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kFormationEffectPaletteSource)));
    m.si() = uint16_t(m.ax());
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kFormationEffectPaletteVariant))));
branch_c828:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_c834;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_c828;
branch_c834:;
    m.ax() = uint16_t(0xC);
    m.cx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.cx() ^ m.cx())));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kFormationEffectPaletteCount))));
    m.di() = uint16_t(kPaletteBuffer);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kFormationEffectWidth))));
    m.cx() = uint16_t((m.cx() & 0x00FF) |
                      (uint16_t(uint8_t(m.rd8(m.ss(), uint16_t(kFormationEffectHeight)))) << 8));
    m.bx() = uint16_t(m.rd16(m.ss(), uint16_t(kFormationFrontPosition)));
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x4);
    if (m.flags() & emu::ZF)
        goto branch_c871;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x6);
    if (m.flags() & emu::ZF)
        goto branch_c864;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x9);
    if (m.flags() & emu::ZF)
        goto branch_c869;
branch_c864:;
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0x1)));
    goto branch_c871;
branch_c869:;
    m.bx() = uint16_t((m.bx() & 0x00FF) |
                      (uint16_t(uint8_t(computed::subtract<uint8_t>(m, uint8_t(m.bx() >> 8), 0x2))) << 8));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0x2)));
    goto branch_c871;
branch_c871:;
    m.wr16(m.ss(), uint16_t(kFormationFrontPosition), uint16_t(m.bx()));
    return;
}
void load_formation_front_layer(Machine &m) {
    load_formation_front_layer_core(m);
    if (m.cs() == 0x6000)
        m.retf();
}
// rom: 0x36CBE2
// Copy AL's formation layer palette into the palette target buffer.
void load_formation_layer_target_palette_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_cbec;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_cc1c;
    goto branch_cc4c;
branch_cbec:;
    m.ax() = uint16_t(0xE000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kFormationFrontPaletteSource)));
    m.si() = uint16_t(m.ax());
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kFormationFrontPaletteVariant))));
branch_cbfc:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_cc08;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_cbfc;
branch_cc08:;
    m.ax() = uint16_t(0x6);
    m.cx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.cx() ^ m.cx())));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kFormationFrontPaletteCount))));
    m.di() = uint16_t(kPaletteTargetBuffer);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_cc7c;
branch_cc1c:;
    m.ax() = uint16_t(0xE000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kFormationMiddlePaletteSource)));
    m.si() = uint16_t(m.ax());
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kFormationMiddlePaletteVariant))));
branch_cc2c:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_cc38;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_cc2c;
branch_cc38:;
    m.ax() = uint16_t(0x5);
    m.cx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.cx() ^ m.cx())));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kFormationMiddlePaletteCount))));
    m.di() = uint16_t(kPaletteTargetBuffer);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_cc7c;
branch_cc4c:;
    m.ax() = uint16_t(0xE000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kFormationBackPaletteSource)));
    m.si() = uint16_t(m.ax());
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kFormationBackPaletteVariant))));
branch_cc5c:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_cc68;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x20));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    goto branch_cc5c;
branch_cc68:;
    m.ax() = uint16_t(0x4);
    m.cx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.cx() ^ m.cx())));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kFormationBackPaletteCount))));
    m.di() = uint16_t(kPaletteTargetBuffer);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_cc7c;
branch_cc7c:;
    return;
}
void load_formation_layer_target_palette(Machine &m) {
    load_formation_layer_target_palette_core(m);
    if (m.cs() == 0x6000)
        m.retf();
}
} // namespace sawgod::game
namespace sawgod::game {
void dispatch_computed_handler_core(Machine &m, uint16_t target) {
    switch (0x300000u + (uint32_t(m.cs()) << 4) + target) {
    case 0x34198B:
        select_party_menu_category_core(m);
        break;
    case 0x341FB7:
        run_inventory_tab_core(m);
        break;
    case 0x342D43:
        run_equipment_tab_core(m);
        break;
    case 0x343BA7:
        run_transfer_tab_core(m);
        break;
    case 0x344AB1:
        run_formation_tab_core(m);
        break;
    case 0x345139:
        run_progress_tab_core(m);
        break;
    case 0x34527B: {
        const uint16_t callback_cs = m.cs();
        run_creation_unlock_browser_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x345FFD:
        run_save_tab_core(m);
        break;
    case 0x346B29:
        run_options_tab_core(m);
        break;
    case 0x346C1A: {
        const uint16_t callback_cs = m.cs();
        run_dash_setting_menu_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x346CC7: {
        const uint16_t callback_cs = m.cs();
        disable_dash_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x346CCF: {
        const uint16_t callback_cs = m.cs();
        run_dash_button_mode_menu_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x346D79: {
        const uint16_t callback_cs = m.cs();
        dash_while_button_held_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x346D8D: {
        const uint16_t callback_cs = m.cs();
        dash_while_button_released_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x346DA7: {
        const uint16_t callback_cs = m.cs();
        run_auto_target_setting_menu_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x346E56: {
        const uint16_t callback_cs = m.cs();
        disable_auto_target_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x346E5E: {
        const uint16_t callback_cs = m.cs();
        enable_auto_target_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x346E6C: {
        const uint16_t callback_cs = m.cs();
        run_monster_preview_setting_menu_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x346F1C: {
        const uint16_t callback_cs = m.cs();
        disable_monster_preview_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x346F24: {
        const uint16_t callback_cs = m.cs();
        enable_monster_preview_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x346F32: {
        const uint16_t callback_cs = m.cs();
        run_dialogue_speed_menu_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x346FDD: {
        const uint16_t callback_cs = m.cs();
        run_dialogue_party_order_menu_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x347138: {
        const uint16_t callback_cs = m.cs();
        reset_dialogue_order_with_sound_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x347747: {
        const uint16_t callback_cs = m.cs();
        run_load_tab_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x348284: {
        const uint16_t callback_cs = m.cs();
        ignore_menu_item_description_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x348295: {
        const uint16_t callback_cs = m.cs();
        draw_inventory_item_description_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x348328: {
        const uint16_t callback_cs = m.cs();
        draw_equipment_item_description_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3483CA: {
        const uint16_t callback_cs = m.cs();
        draw_usable_equipment_description_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x348487: {
        const uint16_t callback_cs = m.cs();
        draw_inventory_command_description_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x348642: {
        const uint16_t callback_cs = m.cs();
        dispatch_field_potion_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x348648: {
        const uint16_t callback_cs = m.cs();
        dispatch_field_hi_potion_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x34864E: {
        const uint16_t callback_cs = m.cs();
        dispatch_field_cure_book_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x348654: {
        const uint16_t callback_cs = m.cs();
        dispatch_field_healing_staff_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x34865A: {
        const uint16_t callback_cs = m.cs();
        dispatch_field_gold_needle_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x348660: {
        const uint16_t callback_cs = m.cs();
        dispatch_field_cross_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x348666: {
        const uint16_t callback_cs = m.cs();
        dispatch_field_eye_drops_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x34866C: {
        const uint16_t callback_cs = m.cs();
        dispatch_field_revive_item_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x348672: {
        const uint16_t callback_cs = m.cs();
        dispatch_field_elixir_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x348678: {
        const uint16_t callback_cs = m.cs();
        dispatch_field_heart_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x34867E: {
        const uint16_t callback_cs = m.cs();
        dispatch_field_sages_stone_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x348684: {
        const uint16_t callback_cs = m.cs();
        dispatch_field_door_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x34868A: {
        const uint16_t callback_cs = m.cs();
        dispatch_field_strength_source_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x348690: {
        const uint16_t callback_cs = m.cs();
        dispatch_field_agility_source_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x348696: {
        const uint16_t callback_cs = m.cs();
        dispatch_field_hp_two_hundred_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x34869C: {
        const uint16_t callback_cs = m.cs();
        dispatch_field_hp_four_hundred_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3486A2: {
        const uint16_t callback_cs = m.cs();
        dispatch_field_hp_six_hundred_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3486A8: {
        const uint16_t callback_cs = m.cs();
        dispatch_field_first_tower_key_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3486AE: {
        const uint16_t callback_cs = m.cs();
        dispatch_field_second_tower_key_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3486B4: {
        const uint16_t callback_cs = m.cs();
        dispatch_field_third_tower_key_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3486BA: {
        const uint16_t callback_cs = m.cs();
        dispatch_field_antidote_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3486C0: {
        const uint16_t callback_cs = m.cs();
        dispatch_field_wake_bell_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3486C6: {
        const uint16_t callback_cs = m.cs();
        dispatch_field_frying_pan_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3486CC: {
        const uint16_t callback_cs = m.cs();
        dispatch_field_paralysis_cure_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3486D2: {
        const uint16_t callback_cs = m.cs();
        dispatch_field_nectar_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3486D8: {
        const uint16_t callback_cs = m.cs();
        dispatch_field_cure_spell_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3486DE: {
        const uint16_t callback_cs = m.cs();
        dispatch_field_healing_spell_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3486E4: {
        const uint16_t callback_cs = m.cs();
        dispatch_field_revival_spell_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3486EA: {
        const uint16_t callback_cs = m.cs();
        dispatch_field_treatment_spell_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3486F0: {
        const uint16_t callback_cs = m.cs();
        dispatch_field_raise_spell_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3486F6: {
        const uint16_t callback_cs = m.cs();
        dispatch_field_teleport_spell_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x348765: {
        const uint16_t callback_cs = m.cs();
        run_monster_history_menu_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x348E1A: {
        const uint16_t callback_cs = m.cs();
        cancel_inn_stay_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x348E22: {
        const uint16_t callback_cs = m.cs();
        purchase_inn_stay_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x349DF5: {
        const uint16_t callback_cs = m.cs();
        run_shop_sell_menu_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x34A057: {
        const uint16_t callback_cs = m.cs();
        run_shop_buy_menu_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x34A28A: {
        const uint16_t callback_cs = m.cs();
        buy_and_equip_shop_item_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x34A473: {
        const uint16_t callback_cs = m.cs();
        buy_shop_item_into_inventory_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x35483E: {
        const uint16_t callback_cs = m.cs();
        dialogue_test_counter_range_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x354863: {
        const uint16_t callback_cs = m.cs();
        dialogue_adjust_party_hp_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3548C6: {
        const uint16_t callback_cs = m.cs();
        dialogue_test_owned_item_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x35492E: {
        const uint16_t callback_cs = m.cs();
        dialogue_update_packed_counter_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x358167: {
        const uint16_t callback_cs = m.cs();
        event_show_dialogue_overlay_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x35818E: {
        const uint16_t callback_cs = m.cs();
        event_continue_dialogue_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3581A8: {
        const uint16_t callback_cs = m.cs();
        event_grant_item_with_message_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x35827E: {
        const uint16_t callback_cs = m.cs();
        event_grant_item_or_discard_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3582A4: {
        const uint16_t callback_cs = m.cs();
        event_open_service_menu_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3582FF: {
        const uint16_t callback_cs = m.cs();
        event_change_map_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x358391:
        travel_area_exit_core(m);
        break;
    case 0x358404: {
        const uint16_t callback_cs = m.cs();
        event_request_sound_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x35845A: {
        const uint16_t callback_cs = m.cs();
        event_start_battle_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x35848A: {
        const uint16_t callback_cs = m.cs();
        event_relocate_vehicle_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3584E2: {
        const uint16_t callback_cs = m.cs();
        event_board_or_leave_vehicle_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3585CC: {
        const uint16_t callback_cs = m.cs();
        event_move_or_turn_character_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x35870D: {
        const uint16_t callback_cs = m.cs();
        event_apply_screen_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3587F2: {
        const uint16_t callback_cs = m.cs();
        event_dispatch_extended_command_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x358832: {
        const uint16_t callback_cs = m.cs();
        event_freeze_and_darken_field_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x35883E: {
        const uint16_t callback_cs = m.cs();
        event_darken_field_palettes_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x35886C: {
        const uint16_t callback_cs = m.cs();
        event_play_immediate_sound_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x358872: {
        const uint16_t callback_cs = m.cs();
        event_set_music_request_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x358877: {
        const uint16_t callback_cs = m.cs();
        event_set_sound_request_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x35887C: {
        const uint16_t callback_cs = m.cs();
        event_run_intro_command_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x358882: {
        const uint16_t callback_cs = m.cs();
        event_start_upward_portamento_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x358895: {
        const uint16_t callback_cs = m.cs();
        event_start_downward_portamento_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3588A8: {
        const uint16_t callback_cs = m.cs();
        event_place_vehicle_at_fixed_destination_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3588C1: {
        const uint16_t callback_cs = m.cs();
        event_set_player_animation_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3588D0: {
        const uint16_t callback_cs = m.cs();
        event_animate_player_gesture_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x358902: {
        const uint16_t callback_cs = m.cs();
        event_set_npc_sprite_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x358929: {
        const uint16_t callback_cs = m.cs();
        event_force_player_step_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x358937: {
        const uint16_t callback_cs = m.cs();
        event_move_player_forward_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x35957E:
        animate_map_tile_strip_core(m);
        break;
    case 0x3595DB: {
        const uint16_t callback_cs = m.cs();
        animate_field_mixed_four_frame_tiles_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x359679: {
        const uint16_t callback_cs = m.cs();
        animate_field_half_rate_two_frame_tiles_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3596D8: {
        const uint16_t callback_cs = m.cs();
        animate_field_nine_strip_two_frame_tiles_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x359776: {
        const uint16_t callback_cs = m.cs();
        animate_field_mixed_eight_frame_tiles_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x35984B: {
        const uint16_t callback_cs = m.cs();
        animate_field_nine_strip_four_frame_tiles_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3598A8: {
        const uint16_t callback_cs = m.cs();
        animate_field_three_strip_eight_frame_tiles_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x359905: {
        const uint16_t callback_cs = m.cs();
        animate_field_two_strip_eight_frame_tiles_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x359962: {
        const uint16_t callback_cs = m.cs();
        animate_field_four_strip_eight_frame_tiles_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3599BF: {
        const uint16_t callback_cs = m.cs();
        animate_field_alternate_two_strip_tiles_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x359A1C: {
        const uint16_t callback_cs = m.cs();
        animate_field_five_strip_eight_frame_tiles_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x359A79: {
        const uint16_t callback_cs = m.cs();
        animate_field_three_strip_sixteen_frame_tiles_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x359AD6: {
        const uint16_t callback_cs = m.cs();
        animate_field_six_strip_two_frame_tiles_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x359B33: {
        const uint16_t callback_cs = m.cs();
        animate_field_fast_sixteen_frame_tiles_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x359B90: {
        const uint16_t callback_cs = m.cs();
        animate_field_fast_two_strip_four_frame_tiles_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x359BED: {
        const uint16_t callback_cs = m.cs();
        animate_field_alternate_fast_four_frame_tiles_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x359C4A: {
        const uint16_t callback_cs = m.cs();
        animate_field_single_four_frame_tile_strip_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x359CB2: {
        const uint16_t callback_cs = m.cs();
        animate_field_progress_dependent_tiles_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x359DFC: {
        const uint16_t callback_cs = m.cs();
        animate_field_mixed_sixteen_frame_tiles_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x359E93: {
        const uint16_t callback_cs = m.cs();
        animate_field_fast_single_four_frame_strip_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x359EF0: {
        const uint16_t callback_cs = m.cs();
        animate_field_single_thirty_two_frame_strip_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x359F4D: {
        const uint16_t callback_cs = m.cs();
        animate_field_four_rate_tile_groups_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x35A052: {
        const uint16_t callback_cs = m.cs();
        animate_field_six_tick_mixed_tiles_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x35A0E9: {
        const uint16_t callback_cs = m.cs();
        animate_field_slow_six_strip_tiles_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x35A146: {
        const uint16_t callback_cs = m.cs();
        animate_field_half_rate_four_frame_tiles_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x35A1DF: {
        const uint16_t callback_cs = m.cs();
        animate_field_mixed_four_and_two_frame_tiles_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x35A276: {
        const uint16_t callback_cs = m.cs();
        animate_field_alternate_four_strip_tiles_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x35A4AA: {
        const uint16_t callback_cs = m.cs();
        animate_field_dual_two_frame_palettes_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x35A51E: {
        const uint16_t callback_cs = m.cs();
        animate_field_palette_one_six_frames_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x35A578: {
        const uint16_t callback_cs = m.cs();
        animate_field_palette_three_twelve_frames_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x35A5D2: {
        const uint16_t callback_cs = m.cs();
        animate_field_palette_three_six_frames_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x35A62C: {
        const uint16_t callback_cs = m.cs();
        animate_field_palette_two_six_frames_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x35A686:
        animate_map_palette_cycle_core(m);
        break;
    case 0x35A6FA: {
        const uint16_t callback_cs = m.cs();
        animate_field_mixed_two_and_six_frame_palettes_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x35A76E: {
        const uint16_t callback_cs = m.cs();
        animate_field_palette_three_four_frames_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x35A7C8: {
        const uint16_t callback_cs = m.cs();
        animate_field_palette_three_two_frames_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3669B1:
        announce_battle_action_once_core(m);
        break;
    case 0x3669E6:
        try_cure_status_40_core(m);
        break;
    case 0x366A40:
        try_cure_status_80_core(m);
        break;
    case 0x366A9A: {
        const uint16_t callback_cs = m.cs();
        execute_defend_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x366B4A: {
        const uint16_t callback_cs = m.cs();
        execute_barrier_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x366C12: {
        const uint16_t callback_cs = m.cs();
        execute_counter_stance_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x366C28: {
        const uint16_t callback_cs = m.cs();
        execute_strength_weapon_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3671BE: {
        const uint16_t callback_cs = m.cs();
        execute_element_slayer_weapon_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x367319: {
        const uint16_t callback_cs = m.cs();
        execute_species_slayer_weapon_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x367463: {
        const uint16_t callback_cs = m.cs();
        execute_hp_drain_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3675DD: {
        const uint16_t callback_cs = m.cs();
        execute_magic_healing_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3676FC: {
        const uint16_t callback_cs = m.cs();
        execute_agility_weapon_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x367851: {
        const uint16_t callback_cs = m.cs();
        execute_strength_technique_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x367929: {
        const uint16_t callback_cs = m.cs();
        execute_shattering_weapon_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3679AE: {
        const uint16_t callback_cs = m.cs();
        execute_revenge_weapon_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x367A55: {
        const uint16_t callback_cs = m.cs();
        execute_strength_ignoring_defense_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x367ADB: {
        const uint16_t callback_cs = m.cs();
        execute_chainsaw_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x367C0D: {
        const uint16_t callback_cs = m.cs();
        execute_firearm_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x367CDC: {
        const uint16_t callback_cs = m.cs();
        execute_bow_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x367D84: {
        const uint16_t callback_cs = m.cs();
        execute_whip_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x367E3B: {
        const uint16_t callback_cs = m.cs();
        execute_heavy_ordnance_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x367EEB: {
        const uint16_t callback_cs = m.cs();
        execute_beam_weapon_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x367F83: {
        const uint16_t callback_cs = m.cs();
        execute_group_ordnance_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x36806B: {
        const uint16_t callback_cs = m.cs();
        execute_group_elimination_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x36811D: {
        const uint16_t callback_cs = m.cs();
        execute_potion_healing_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3681D7: {
        const uint16_t callback_cs = m.cs();
        execute_status_cure_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3684D7: {
        const uint16_t callback_cs = m.cs();
        execute_elemental_magic_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x368815: {
        const uint16_t callback_cs = m.cs();
        execute_status_magic_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x368943: {
        const uint16_t callback_cs = m.cs();
        execute_scripture_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3689DC: {
        const uint16_t callback_cs = m.cs();
        execute_magic_weapon_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x368B6D: {
        const uint16_t callback_cs = m.cs();
        execute_multi_hit_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x368D23: {
        const uint16_t callback_cs = m.cs();
        execute_physical_status_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x368F5E: {
        const uint16_t callback_cs = m.cs();
        execute_stat_reduction_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3690C8: {
        const uint16_t callback_cs = m.cs();
        execute_regeneration_stance_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x36910C: {
        const uint16_t callback_cs = m.cs();
        execute_innate_magic_damage_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x369268: {
        const uint16_t callback_cs = m.cs();
        execute_self_destruct_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x369458: {
        const uint16_t callback_cs = m.cs();
        execute_gold_theft_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3694FF: {
        const uint16_t callback_cs = m.cs();
        execute_full_self_healing_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x369578: {
        const uint16_t callback_cs = m.cs();
        execute_nonhuman_only_damage_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x36960F: {
        const uint16_t callback_cs = m.cs();
        execute_group_drain_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x369931: {
        const uint16_t callback_cs = m.cs();
        execute_defense_increase_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x369972: {
        const uint16_t callback_cs = m.cs();
        execute_strength_replacement_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3699DF: {
        const uint16_t callback_cs = m.cs();
        execute_defense_replacement_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x369A54: {
        const uint16_t callback_cs = m.cs();
        execute_unanimated_fixed_damage_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x369A8A: {
        const uint16_t callback_cs = m.cs();
        execute_consume_target_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x369B18: {
        const uint16_t callback_cs = m.cs();
        execute_no_effect_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x369B49: {
        const uint16_t callback_cs = m.cs();
        execute_agility_increase_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x369BAA: {
        const uint16_t callback_cs = m.cs();
        execute_flee_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x369C37: {
        const uint16_t callback_cs = m.cs();
        execute_immunity_checked_status_action_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C0109: {
        const uint16_t callback_cs = m.cs();
        turn_intro_player_from_facing_table_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C02BA: {
        const uint16_t callback_cs = m.cs();
        animate_intro_falling_particles_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C02C0: {
        const uint16_t callback_cs = m.cs();
        animate_intro_stationary_particles_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C02C6: {
        const uint16_t callback_cs = m.cs();
        animate_intro_return_particles_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C0382: {
        const uint16_t callback_cs = m.cs();
        animate_intro_projectile_burst_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C0555: {
        const uint16_t callback_cs = m.cs();
        darken_intro_effect_palette_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C0580: {
        const uint16_t callback_cs = m.cs();
        initialize_intro_falling_particles_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C061F: {
        const uint16_t callback_cs = m.cs();
        update_intro_falling_particles_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C06BE: {
        const uint16_t callback_cs = m.cs();
        draw_intro_moving_effect_sprite_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C06CE: {
        const uint16_t callback_cs = m.cs();
        animate_intro_horizontal_projectile_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C0760: {
        const uint16_t callback_cs = m.cs();
        clear_intro_primary_callback_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C0767: {
        const uint16_t callback_cs = m.cs();
        scroll_intro_field_up_and_back_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C07D6: {
        const uint16_t callback_cs = m.cs();
        animate_intro_orbiting_sprite_scene_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C0B3D: {
        const uint16_t callback_cs = m.cs();
        animate_intro_window_split_transition_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C0C85: {
        const uint16_t callback_cs = m.cs();
        animate_intro_split_fade_transition_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C0D0F: {
        const uint16_t callback_cs = m.cs();
        run_intro_white_flash_battle_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C0DBD: {
        const uint16_t callback_cs = m.cs();
        animate_intro_upper_sprite_strip_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C0ECC: {
        const uint16_t callback_cs = m.cs();
        animate_intro_lower_sprite_strip_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C0F98: {
        const uint16_t callback_cs = m.cs();
        start_intro_periodic_sound_fade_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C0FE3: {
        const uint16_t callback_cs = m.cs();
        update_intro_periodic_sound_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C0FF9: {
        const uint16_t callback_cs = m.cs();
        start_intro_secondary_moving_sprite_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C1042: {
        const uint16_t callback_cs = m.cs();
        finish_intro_secondary_moving_sprite_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C1087: {
        const uint16_t callback_cs = m.cs();
        draw_intro_secondary_moving_sprite_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C10C7: {
        const uint16_t callback_cs = m.cs();
        animate_intro_map_progress_flash_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C1137: {
        const uint16_t callback_cs = m.cs();
        start_intro_random_sprite_animation_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C113F: {
        const uint16_t callback_cs = m.cs();
        update_intro_random_sprite_animation_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C1174: {
        const uint16_t callback_cs = m.cs();
        stop_intro_random_sprite_animation_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C11A4: {
        const uint16_t callback_cs = m.cs();
        move_intro_first_npc_up_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C11CE: {
        const uint16_t callback_cs = m.cs();
        move_intro_player_to_center_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C11E3: {
        const uint16_t callback_cs = m.cs();
        move_intro_three_npcs_away_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C126A: {
        const uint16_t callback_cs = m.cs();
        animate_intro_npc_departure_scene_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C1644: {
        const uint16_t callback_cs = m.cs();
        animate_intro_player_departure_strip_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C172E: {
        const uint16_t callback_cs = m.cs();
        animate_intro_strip_at_right_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C185C: {
        const uint16_t callback_cs = m.cs();
        animate_intro_strip_at_left_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C198A: {
        const uint16_t callback_cs = m.cs();
        animate_intro_wave_particles_and_warp_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C1AF0: {
        const uint16_t callback_cs = m.cs();
        run_intro_battle_with_npc_restore_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C1B60: {
        const uint16_t callback_cs = m.cs();
        animate_intro_large_sprite_overlay_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C1CC6: {
        const uint16_t callback_cs = m.cs();
        board_intro_vehicle_and_warp_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C1D25: {
        const uint16_t callback_cs = m.cs();
        start_intro_scrolling_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C1D7D: {
        const uint16_t callback_cs = m.cs();
        update_intro_scrolling_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C1DFA: {
        const uint16_t callback_cs = m.cs();
        start_intro_static_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C1E2A: {
        const uint16_t callback_cs = m.cs();
        draw_intro_static_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C1E73: {
        const uint16_t callback_cs = m.cs();
        run_intro_battle_and_resume_music_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C1EC3: {
        const uint16_t callback_cs = m.cs();
        animate_intro_progress_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C1FAA: {
        const uint16_t callback_cs = m.cs();
        animate_intro_alternate_sprite_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C2086: {
        const uint16_t callback_cs = m.cs();
        animate_intro_particles_with_dual_overlays_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C2147: {
        const uint16_t callback_cs = m.cs();
        draw_intro_first_overlay_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C2187: {
        const uint16_t callback_cs = m.cs();
        draw_intro_second_overlay_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C21CC: {
        const uint16_t callback_cs = m.cs();
        finish_intro_dual_overlay_scene_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C2229: {
        const uint16_t callback_cs = m.cs();
        start_intro_oscillating_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C2268: {
        const uint16_t callback_cs = m.cs();
        update_intro_oscillating_grid_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C22C0: {
        const uint16_t callback_cs = m.cs();
        start_intro_multi_grid_sequence_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C2326: {
        const uint16_t callback_cs = m.cs();
        update_intro_multi_grid_sequence_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C2475: {
        const uint16_t callback_cs = m.cs();
        animate_intro_npc_grid_flash_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C2552: {
        const uint16_t callback_cs = m.cs();
        animate_intro_screen_overlay_progress_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C2695: {
        const uint16_t callback_cs = m.cs();
        start_intro_fixed_encounter_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C26B2: {
        const uint16_t callback_cs = m.cs();
        scroll_intro_field_with_sound_fade_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C2701: {
        const uint16_t callback_cs = m.cs();
        animate_intro_two_stage_sprite_effect_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C27E0: {
        const uint16_t callback_cs = m.cs();
        run_intro_battle_and_random_particles_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C29A4: {
        const uint16_t callback_cs = m.cs();
        animate_intro_random_particles_with_progress_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C2B13: {
        const uint16_t callback_cs = m.cs();
        animate_intro_random_particles_with_sound_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C2C81: {
        const uint16_t callback_cs = m.cs();
        start_intro_upper_left_overlay_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C2CB1: {
        const uint16_t callback_cs = m.cs();
        draw_intro_upper_left_overlay_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C2CF1: {
        const uint16_t callback_cs = m.cs();
        clear_intro_frame_callbacks_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C2D00: {
        const uint16_t callback_cs = m.cs();
        play_intro_vignette_first_variant_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C2D07: {
        const uint16_t callback_cs = m.cs();
        play_intro_vignette_second_variant_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C2D0E: {
        const uint16_t callback_cs = m.cs();
        play_intro_vignette_third_variant_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C2D15: {
        const uint16_t callback_cs = m.cs();
        play_intro_vignette_fourth_variant_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C2D1C: {
        const uint16_t callback_cs = m.cs();
        play_intro_vignette_fifth_variant_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C32A6: {
        const uint16_t callback_cs = m.cs();
        mark_intro_first_npc_script_controlled_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C32AD: {
        const uint16_t callback_cs = m.cs();
        mark_intro_other_npcs_script_controlled_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C32C0: {
        const uint16_t callback_cs = m.cs();
        move_intro_player_to_fixed_column_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C32CA: {
        const uint16_t callback_cs = m.cs();
        darken_all_intro_palettes_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C32F0: {
        const uint16_t callback_cs = m.cs();
        fade_intro_sound_and_palettes_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C3318: {
        const uint16_t callback_cs = m.cs();
        animate_intro_player_rising_scene_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C33E0: {
        const uint16_t callback_cs = m.cs();
        run_intro_final_battle_and_music_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C3430: {
        const uint16_t callback_cs = m.cs();
        fade_intro_sound_with_field_frames_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C3457: {
        const uint16_t callback_cs = m.cs();
        prepare_intro_scene_sequence_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C347F: {
        const uint16_t callback_cs = m.cs();
        play_intro_scene_sequence_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C3854: {
        const uint16_t callback_cs = m.cs();
        show_intro_completion_panel_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    case 0x3C38C0: {
        const uint16_t callback_cs = m.cs();
        run_ending_menu_sequence_core(m);
        if (m.cs() != callback_cs)
            return;
    } break;
    default:
        m.call_near(target);
        break; // Out-of-domain pointer, never a table code entry.
    }
}
} // namespace sawgod::game
