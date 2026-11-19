// Decompiled menu, character-creation, and shared drawing routines.
#include "game/bank34_misc.h"
#include "game/bank34.h"
#include "game/bank35_field.h"
#include "game/bank35_world.h"
#include "game/bank36_battle.h"
#include "game/bank36_misc.h"
#include "game/bank37.h"
#include "game/bank3c_title.h"
#include "game/bank3d_sound.h"
#include "game/computed_flags.h"
#include "game/computed_handlers.h"
#include "game/hooks.h"
#include "game/save_slots.h"
#include "game/session.h"
#include "game/system.h"
#include "game/tables.h"
#include "game/text.h"
#include "game/text_render.h"
#include "game/text_tokens.h"
#include "game/ui_scene.h"
#include <bit>

namespace sawgod::game {
namespace {
constexpr uint16_t kMenuScreenBackup = 0x2800;
constexpr uint16_t kOverlayScreen = 0x2000;
constexpr uint16_t kScreenMap2 = 0x1800;
constexpr uint16_t kScreenMap = 0x1000;
constexpr uint16_t kItemIconTiles = 0xC0E7;          // bank-relative ROM source
constexpr uint16_t kItemPrices = 0xC127;             // bank-relative ROM source
constexpr uint16_t kShopStock = 0xC527;              // bank-relative ROM source
constexpr uint16_t kPartyMenuPalettes = 0xC81B;      // bank-relative ROM source
constexpr uint16_t kShopMenuPalettes = 0xC85B;       // bank-relative ROM source
constexpr uint16_t kMenuBackgroundPalettes = 0xC89B; // bank-relative ROM source
constexpr uint16_t kMonsterPointers = 0xD1FB;        // bank-relative ROM source
constexpr uint16_t kMenuCursorPosition = 0x014A;     // menu_cursor_position
constexpr uint16_t kSpriteBuffer = 0xC000; // sprite staging; also the base of the masked C000-FFFF RAM window
[[maybe_unused]] constexpr uint16_t kSpriteSixTileAttribute = 0xC019;      // sprite_six_tile_attribute
constexpr uint16_t kSpriteBackup = 0xC200;                                 // sprite_backup
constexpr uint16_t kPaletteBuffer = 0xC400;                                // palette_buffer
constexpr uint16_t kPaletteTarget = 0xC600;                                // palette_target_buffer
constexpr uint16_t kKeysPressed = 0xC804;                                  // keys_pressed
constexpr uint16_t kKeysRepeat = 0xC806;                                   // keys_repeat
constexpr uint16_t kKeyRepeatTimer = 0xC808;                               // key_repeat_timer
[[maybe_unused]] constexpr uint16_t kFrames = 0xC80C;                      // random_seed
constexpr uint16_t kOamCount = 0xC80F;                                     // sprite_unit_count
[[maybe_unused]] constexpr uint16_t kPlayerFacing = 0xC810;                // player_facing
[[maybe_unused]] constexpr uint16_t kBlinkEnabled = 0xC811;                // player_blink_enabled
[[maybe_unused]] constexpr uint16_t kFieldOverlayMode = 0xC812;            // field_overlay_mode
[[maybe_unused]] constexpr uint16_t kFieldWindowTopLeft = 0xC814;          // field_window_top_left
[[maybe_unused]] constexpr uint16_t kFieldWindowBottomRight = 0xC816;      // field_window_bottom_right
[[maybe_unused]] constexpr uint16_t kSavedSceneSp = 0xC818;                // intro_saved_sp
[[maybe_unused]] constexpr uint16_t kSceneIndex = 0xC81A;                  // scene_index
[[maybe_unused]] constexpr uint16_t kIntroStrip = 0xC81C;                  // intro_strip
[[maybe_unused]] constexpr uint16_t kBattleSceneScratch = 0xC81E;          // battle_scene_scratch
[[maybe_unused]] constexpr uint16_t kMusicRequest = 0xC820;                // music_request
constexpr uint16_t kSfxRequest = 0xC821;                                   // sfx_request
[[maybe_unused]] constexpr uint16_t kMusicRequestPrev = 0xC822;            // music_current
[[maybe_unused]] constexpr uint16_t kFieldEventPending = 0xC825;           // field_event_pending
[[maybe_unused]] constexpr uint16_t kFieldEventKind = 0xC826;              // field_event_kind
[[maybe_unused]] constexpr uint16_t kFieldEventArgument = 0xC827;          // field_event_argument
[[maybe_unused]] constexpr uint16_t kInteractionState = 0xC82A;            // field_interaction_state
constexpr uint16_t kDialogueSkipDelay = 0xC82B;                            // dialogue_skip_delay
constexpr uint16_t kGameState = 0xD000;                                    // game_state
constexpr uint16_t kMemberName = 0xD001;                                   // member_name
constexpr uint16_t kMemberType = 0xD005;                                   // member0_race
constexpr uint16_t kMemberHp = 0xD006;                                     // member0_hp
constexpr uint16_t kMemberEquipmentFlags = 0xD00A;                         // member_equipment_flags
constexpr uint16_t kMemberStrength = 0xD00B;                               // member0_strength
constexpr uint16_t kMember0Equipment = 0xD00F;                             // member0_equipment
constexpr uint16_t kMember1Record = 0xD01F;                                // member1_record
constexpr uint16_t kSecondMemberEquipmentFlags = 0xD029;                   // second_member_equipment_flags
constexpr uint16_t kMember2Record = 0xD03E;                                // member2_record
constexpr uint16_t kThirdMemberEquipmentFlags = 0xD048;                    // third_member_equipment_flags
constexpr uint16_t kMember3Record = 0xD05D;                                // member3_record
constexpr uint16_t kFourthMemberEquipmentFlags = 0xD067;                   // fourth_member_equipment_flags
constexpr uint16_t kInventory = 0xD07C;                                    // inventory_slots
constexpr uint16_t kInventorySlot0Uses = 0xD07D;                           // inventory_slot_0_uses
constexpr uint16_t kInventorySlot1Id = 0xD07E;                             // inventory_slot_1_id
constexpr uint16_t kInventorySlot1Uses = 0xD07F;                           // inventory_slot_1_uses
constexpr uint16_t kInventorySlot2Id = 0xD080;                             // inventory_slot_2_id
constexpr uint16_t kInventorySlot2Uses = 0xD081;                           // inventory_slot_2_uses
constexpr uint16_t kInventorySlot3Id = 0xD082;                             // inventory_slot_3_id
constexpr uint16_t kInventorySlot3Uses = 0xD083;                           // inventory_slot_3_uses
constexpr uint16_t kInventorySlot4Id = 0xD084;                             // inventory_slot_4_id
constexpr uint16_t kInventorySlot4Uses = 0xD085;                           // inventory_slot_4_uses
constexpr uint16_t kInventorySlot5Id = 0xD086;                             // inventory_slot_5_id
constexpr uint16_t kInventorySlot5Uses = 0xD087;                           // inventory_slot_5_uses
constexpr uint16_t kInventorySlot6Id = 0xD088;                             // inventory_slot_6_id
constexpr uint16_t kInventorySlot6Uses = 0xD089;                           // inventory_slot_6_uses
constexpr uint16_t kInventorySlot7Id = 0xD08A;                             // inventory_slot_7_id
constexpr uint16_t kInventorySlot7Uses = 0xD08B;                           // inventory_slot_7_uses
constexpr uint16_t kKeyItems = 0xD08C;                                     // key_items
constexpr uint16_t kGoldLow = 0xD0A2;                                      // gold
constexpr uint16_t kGoldHigh = 0xD0A4;                                     // gold_high
constexpr uint16_t kPackedProgress = 0xD0A6;                               // packed_progress
[[maybe_unused]] constexpr uint16_t kSecondVehicleMap = 0xD0B3;            // second_vehicle_map
[[maybe_unused]] constexpr uint16_t kSecondVehicleX = 0xD0B4;              // second_vehicle_x
[[maybe_unused]] constexpr uint16_t kSecondVehicleY = 0xD0B5;              // second_vehicle_y
[[maybe_unused]] constexpr uint16_t kSecondVehicleState = 0xD0B6;          // second_vehicle_state
[[maybe_unused]] constexpr uint16_t kTravelMode = 0xD0D6;                  // travel_mode
[[maybe_unused]] constexpr uint16_t kPreviousMap = 0xD0D7;                 // previous_map
[[maybe_unused]] constexpr uint16_t kPreviousPlayerX = 0xD0D8;             // previous_player_x
[[maybe_unused]] constexpr uint16_t kPreviousPlayerY = 0xD0D9;             // previous_player_y
constexpr uint16_t kTowerCounter = 0xD0DA;                                 // tower_counter
[[maybe_unused]] constexpr uint16_t kMapId = 0xD0DC;                       // current_map
[[maybe_unused]] constexpr uint16_t kPlayerCell = 0xD0DD;                  // player_x
[[maybe_unused]] constexpr uint16_t kPlayerY = 0xD0DE;                     // player_y
constexpr uint16_t kDialogueLeader = 0xD0E3;                               // dialogue_leader
[[maybe_unused]] constexpr uint16_t kEndingSeen = 0xD0E5;                  // ending_seen
constexpr uint16_t kDialogueSpeed = 0xD0E6;                                // dialogue_speed
constexpr uint16_t kOptions = 0xD0E7;                                      // field_options
constexpr uint16_t kPartyOrder = 0xD0E8;                                   // dialogue_party_order
constexpr uint16_t kPartyOrderLast = 0xD0EB;                               // dialogue_party_order_last
constexpr uint16_t kEventFlags = 0xD0EC;                                   // event_flags
constexpr uint16_t kMemberQueues = 0xD105;                                 // member_queues
constexpr uint16_t kSaveMagic1 = 0xD435;                                   // save_magic_first
constexpr uint16_t kSaveMagic2 = 0xD436;                                   // save_magic_second
[[maybe_unused]] constexpr uint16_t kPlayerWalkToggle = 0xD500;            // player_walk_toggle
[[maybe_unused]] constexpr uint16_t kFieldPreSpriteCallback = 0xD501;      // field_pre_sprite_callback
[[maybe_unused]] constexpr uint16_t kFormationBackPaletteSource = 0xD502;  // formation_back_palette_source
[[maybe_unused]] constexpr uint16_t kFieldPostSpriteCallback = 0xD503;     // field_post_sprite_callback
[[maybe_unused]] constexpr uint16_t kFormationBackMapSource = 0xD504;      // formation_back_map_source
[[maybe_unused]] constexpr uint16_t kFieldNpcCallback = 0xD505;            // field_npc_callback
[[maybe_unused]] constexpr uint16_t kFormationBackTileCount = 0xD506;      // formation_back_tile_count
[[maybe_unused]] constexpr uint16_t kFormationBackPaletteVariant = 0xD508; // formation_back_palette_variant
[[maybe_unused]] constexpr uint16_t kBattleWipeScroll = 0xD509;            // battle_wipe_scroll
[[maybe_unused]] constexpr uint16_t kBattleWipeFirstX = 0xD50A;            // battle_wipe_first_x
[[maybe_unused]] constexpr uint16_t kBattleWipeSecondScroll = 0xD50B;      // battle_wipe_second_scroll
[[maybe_unused]] constexpr uint16_t kBattleWipeSecondX = 0xD50C;           // battle_wipe_second_x
[[maybe_unused]] constexpr uint16_t kBattleWipeThirdScroll = 0xD50D;       // battle_wipe_third_scroll
[[maybe_unused]] constexpr uint16_t kBattleWipeThirdX = 0xD50E;            // formation_middle_palette_source
[[maybe_unused]] constexpr uint16_t kBattleWipeFourthScroll = 0xD50F;      // battle_wipe_fourth_scroll
[[maybe_unused]] constexpr uint16_t kBattleWipeFourthX = 0xD510;           // battle_wipe_fourth_x
[[maybe_unused]] constexpr uint16_t kFieldScroll = 0xD511;                 // field_scroll
[[maybe_unused]] constexpr uint16_t kFormationMiddleTileCount = 0xD512;    // formation_middle_tile_count
[[maybe_unused]] constexpr uint16_t kIntroOverlayProgress = 0xD513;        // intro_overlay_progress
[[maybe_unused]] constexpr uint16_t kFormationMiddlePaletteVariant =
    0xD514;                                                                // formation_middle_palette_variant
[[maybe_unused]] constexpr uint16_t kMoveDelta = 0xD515;                   // player_move_delta
[[maybe_unused]] constexpr uint16_t kFormationMiddleHeight = 0xD516;       // formation_middle_height
[[maybe_unused]] constexpr uint16_t kMoveShift = 0xD517;                   // player_move_shift
[[maybe_unused]] constexpr uint16_t kFormationFrontTileSource = 0xD518;    // formation_front_tile_source
[[maybe_unused]] constexpr uint16_t kFieldFadeActive = 0xD519;             // field_fade_active
[[maybe_unused]] constexpr uint16_t kFieldPaletteFirstFrame = 0xD51A;      // field_palette_first_frame
[[maybe_unused]] constexpr uint16_t kFieldPaletteTimer = 0xD51B;           // field_palette_timer
[[maybe_unused]] constexpr uint16_t kFieldPaletteSecondFrame = 0xD51C;     // field_palette_second_frame
[[maybe_unused]] constexpr uint16_t kFieldTileAnimationFrame = 0xD51E;     // field_tile_animation_frame
[[maybe_unused]] constexpr uint16_t kFieldTileAnimationTimer = 0xD51F;     // field_tile_animation_timer
[[maybe_unused]] constexpr uint16_t kSpriteSetIndex = 0xD520;              // player_sprite_set
[[maybe_unused]] constexpr uint16_t kPlayerSpriteFacing = 0xD521;          // player_sprite_facing
[[maybe_unused]] constexpr uint16_t kOamBaseY = 0xD522;                    // player_sprite_y
[[maybe_unused]] constexpr uint16_t kFormationFrontPaletteCount = 0xD523;  // formation_front_palette_count
[[maybe_unused]] constexpr uint16_t kOamBaseX = 0xD524;                    // player_sprite_x
[[maybe_unused]] constexpr uint16_t kBlinkToggleA = 0xD526;                // player_blink_toggle_a
[[maybe_unused]] constexpr uint16_t kAnimationStep = 0xD528;               // player_blink_period_a
[[maybe_unused]] constexpr uint16_t kPlayerStepped = 0xD52A;               // player_stepped
[[maybe_unused]] constexpr uint16_t kOverlayEnabled = 0xD52B;              // field_overlay_enabled
[[maybe_unused]] constexpr uint16_t kScriptActive = 0xD52C;                // field_script_active
[[maybe_unused]] constexpr uint16_t kFieldMapSource = 0xD52E;              // formation_effect_palette_variant
[[maybe_unused]] constexpr uint16_t kFormationEffectWidth = 0xD52F;        // formation_effect_width
[[maybe_unused]] constexpr uint16_t kFieldMetatileSource = 0xD530;         // formation_effect_height
[[maybe_unused]] constexpr uint16_t kFormationEffectPaletteCount = 0xD531; // formation_effect_palette_count
[[maybe_unused]] constexpr uint16_t kFieldAnimationPaletteSource = 0xD538; // field_animation_palette_source
[[maybe_unused]] constexpr uint16_t kCamX = 0xD59D;                        // camera_x
[[maybe_unused]] constexpr uint16_t kCamY = 0xD59F;                        // camera_y
[[maybe_unused]] constexpr uint16_t kNpcLastMatch = 0xD5A1;                // npc_last_match
[[maybe_unused]] constexpr uint16_t kRasterWaveOffset = 0xD6B5;            // raster_wave_offset
[[maybe_unused]] constexpr uint16_t kRasterWavePhase = 0xD6B6;             // raster_wave_phase
[[maybe_unused]] constexpr uint16_t kRasterWaveSource = 0xD6B7;            // raster_wave_source
[[maybe_unused]] constexpr uint16_t kEnemyGroupCount = 0xD6D4;             // enemy_group_count
[[maybe_unused]] constexpr uint16_t kEncounterVariant = 0xD6EE;            // encounter_variant
constexpr uint16_t kMenuCursorAnchor = 0xD6FC;                             // menu_cursor_anchor
[[maybe_unused]] constexpr uint16_t kLzDictionary =
    0xD6FE; // shared field scratch: LZ window, metatile palettes, item-message text
[[maybe_unused]] constexpr uint16_t kIntroProjectileTicks = 0xD6FF;     // intro_projectile_ticks
[[maybe_unused]] constexpr uint16_t kPartyMenuEntryContext = 0xD700;    // intro_projectile_y
constexpr uint16_t kPartyMenuText = 0xD701;                             // party_menu_text
[[maybe_unused]] constexpr uint16_t kIntroProjectileState = 0xD702;     // intro_projectile_state
[[maybe_unused]] constexpr uint16_t kIntroProjectileXOffset = 0xD703;   // intro_projectile_x_offset
[[maybe_unused]] constexpr uint16_t kIntroProjectilePhase = 0xD705;     // intro_projectile_phase
[[maybe_unused]] constexpr uint16_t kIntroProjectileFrame = 0xD706;     // intro_projectile_frame
[[maybe_unused]] constexpr uint16_t kIntroProjectileYOffset = 0xD707;   // intro_projectile_y_offset
[[maybe_unused]] constexpr uint16_t kIntroProjectilePosition = 0xD708;  // intro_projectile_position
constexpr uint16_t kMemberTypeText = 0xD709;                            // member_type_text
[[maybe_unused]] constexpr uint16_t kIntroProjectileOrigin = 0xD70A;    // intro_projectile_origin
[[maybe_unused]] constexpr uint16_t kIntroProjectileStep = 0xD70C;      // intro_projectile_step
[[maybe_unused]] constexpr uint16_t kFormationKind = 0xD70D;            // formation_kind
[[maybe_unused]] constexpr uint16_t kBattleFlags = 0xD70E;              // battle_flags
constexpr uint16_t kMeatSourceType = 0xD719;                            // member_hp_text
constexpr uint16_t kMemberRaceText = 0xD71D;                            // member_race_text
constexpr uint16_t kMemberStatText = 0xD722;                            // member_stat_text
constexpr uint16_t kMemberEquipmentText = 0xD732;                       // member_equipment_text
constexpr uint16_t kGoldText = 0xD7EF;                                  // gold_text
constexpr uint16_t kTowerCounterText = 0xD7FF;                          // tower_counter_text
constexpr uint16_t kTowerProgressText = 0xD803;                         // tower_progress_text
constexpr uint16_t kPlayMinutesText = 0xD807;                           // play_minutes_text
constexpr uint16_t kPlaySecondsText = 0xD80B;                           // play_seconds_text
constexpr uint16_t kItemDescriptionText = 0xD80F;                       // item_description_text
constexpr uint16_t kDigitBuffer = 0xD82B;                               // digit_buffer
constexpr uint16_t kMenuDescriptionMode = 0xD83F;                       // menu_description_mode
constexpr uint16_t kMenuActionIndex = 0xD840;                           // menu_action_index
constexpr uint16_t kMenuSelection = 0xD841;                             // race_select_index
constexpr uint16_t kSaveSlotSelection = 0xD842;                         // class_select_index
constexpr uint16_t kSaveChoice = 0xD843;                                // gender_select_index
constexpr uint16_t kMenuThirdSelection = 0xD844;                        // menu_third_selection
constexpr uint16_t kMenuFourthSelection = 0xD845;                       // menu_fourth_selection
constexpr uint16_t kMenuFifthSelection = 0xD846;                        // menu_fifth_selection
constexpr uint16_t kMenuSixthSelection = 0xD847;                        // menu_sixth_selection
constexpr uint16_t kSecondaryCursorPosition = 0xD848;                   // secondary_cursor_position
constexpr uint16_t kMenuExitRequested = 0xD84A;                         // menu_exit_requested
constexpr uint16_t kMenuActionResult = 0xD84B;                          // menu_action_result
constexpr uint16_t kMenuPriceLow = 0xD84C;                              // menu_price_low
constexpr uint16_t kMenuPriceHigh = 0xD84E;                             // menu_price_high
constexpr uint16_t kMenuDescriptionSpriteStart = 0xD850;                // menu_description_sprite_start
constexpr uint16_t kMenuSavedSpriteCount = 0xD851;                      // menu_saved_sprite_count
constexpr uint16_t kCreationLeader = 0xD852;                            // creation_leader
constexpr uint16_t kCreationRace = 0xD853;                              // creation_race
constexpr uint16_t kNamePage = 0xD854;                                  // name_page
constexpr uint16_t kNameInsertionIndex = 0xD855;                        // name_insertion_index
constexpr uint16_t kNameText = 0xD856;                                  // name_text
constexpr uint16_t kMenuSecondItemBuffer = 0xD857;                      // menu_second_item_buffer
constexpr uint16_t kMenuTransferState = 0xD859;                         // menu_transfer_state
constexpr uint16_t kTabTransferCount = 0xD85A;                          // name_caret
constexpr uint16_t kTabTransferPage = 0xD85B;                           // menu_transfer_page
constexpr uint16_t kShopCursorBlink = 0xD85D;                           // shop_cursor_blink
constexpr uint16_t kSaveMinutes = 0xD85E;                               // save_minutes
constexpr uint16_t kSaveSeconds = 0xD85F;                               // save_seconds
constexpr uint16_t kSaveScratch = 0xD8FE;                               // save_scratch
constexpr uint16_t kSavedTowerCounter = 0xD9D8;                         // saved_tower_counter
constexpr uint16_t kSavedTowerProgress = 0xD9D9;                        // saved_tower_progress
constexpr uint16_t kSavedClockLow = 0xD9DD;                             // saved_clock_low
constexpr uint16_t kSavedClockHigh = 0xD9DF;                            // saved_clock_high
constexpr uint16_t kSavedLeader = 0xD9E1;                               // saved_leader
constexpr uint16_t kSavedMarker = 0xD9E3;                               // saved_marker
[[maybe_unused]] constexpr uint16_t kBattleStatusSuccessCount = 0xDBF3; // battle_status_success_count
[[maybe_unused]] constexpr uint16_t kItemUseCount = 0xDBF4;             // item_use_count
[[maybe_unused]] constexpr uint16_t kBattleKnockoutMode = 0xDBF6;       // battle_knockout_mode
[[maybe_unused]] constexpr uint16_t kBattleFlashCounter = 0xDBF7;       // battle_flash_counter
[[maybe_unused]] constexpr uint16_t kBattleGroupEliminationScratch =
    0xDBF9;                                                            // battle_group_elimination_scratch
[[maybe_unused]] constexpr uint16_t kBattleEffectReflected = 0xDBFA;   // battle_effect_reflected
[[maybe_unused]] constexpr uint16_t kBattleReflectionActor = 0xDBFB;   // battle_reflection_actor
[[maybe_unused]] constexpr uint16_t kActionId = 0xDBFC;                // action_id
[[maybe_unused]] constexpr uint16_t kActionEffect = 0xDBFD;            // action_effect_id
[[maybe_unused]] constexpr uint16_t kActingActor = 0xDBFE;             // acting_actor
[[maybe_unused]] constexpr uint16_t kActionTarget = 0xDBFF;            // action_target
[[maybe_unused]] constexpr uint16_t kTargetGroup = 0xDC00;             // target_group
[[maybe_unused]] constexpr uint16_t kBattleSavedAttackValue = 0xDC09;  // battle_saved_attack_value
[[maybe_unused]] constexpr uint16_t kBattleItemPower = 0xDC0A;         // battle_item_power
[[maybe_unused]] constexpr uint16_t kMeterInputTarget = 0xDC0B;        // battle_amount
[[maybe_unused]] constexpr uint16_t kBattleMultiHitCount = 0xDC11;     // battle_multi_hit_count
[[maybe_unused]] constexpr uint16_t kEffectPaletteVariant = 0xDC12;    // effect_palette_variant
[[maybe_unused]] constexpr uint16_t kEffectTicks = 0xDC18;             // effect_ticks
[[maybe_unused]] constexpr uint16_t kEffectFrame = 0xDC19;             // effect_frame
[[maybe_unused]] constexpr uint16_t kEffectX = 0xDCBB;                 // effect_x
[[maybe_unused]] constexpr uint16_t kEffectY = 0xDCBC;                 // effect_y
[[maybe_unused]] constexpr uint16_t kEffectRowY = 0xDCBD;              // effect_row_y
[[maybe_unused]] constexpr uint16_t kEffectHeight = 0xDCBE;            // effect_height
[[maybe_unused]] constexpr uint16_t kEffectSpriteOffset = 0xDCBF;      // effect_sprite_offset
[[maybe_unused]] constexpr uint16_t kGroupDrainSavedTarget = 0xDCC1;   // group_drain_saved_target
[[maybe_unused]] constexpr uint16_t kTextColorScratch = 0xDCD3;        // text_color_scratch
[[maybe_unused]] constexpr uint16_t kBattleImmunityMode = 0xDCD4;      // battle_immunity_mode
[[maybe_unused]] constexpr uint16_t kEffectMotionCounter = 0xDCEC;     // effect_motion_counter
[[maybe_unused]] constexpr uint16_t kEffectTravelOffset = 0xDCFC;      // effect_travel_offset
[[maybe_unused]] constexpr uint16_t kDialogueCursorPosition = 0xDD28;  // dialogue_cursor_position
[[maybe_unused]] constexpr uint16_t kFlarePhaseTicks = 0xDD29;         // flare_phase_ticks
[[maybe_unused]] constexpr uint16_t kEffectTargetMode = 0xDD73;        // effect_target_mode
[[maybe_unused]] constexpr uint16_t kFooterPanelMode = 0xEC83;         // footer_panel_mode
[[maybe_unused]] constexpr uint16_t kGroupDrainTargetCount = 0xEC84;   // group_drain_target_count
[[maybe_unused]] constexpr uint16_t kGroupDrainKnockoutCount = 0xEC85; // group_drain_knockout_count
[[maybe_unused]] constexpr uint16_t kGroupDrainPreviousGroup = 0xEC86; // group_drain_previous_group
void misc_arithmetic_flags(Machine &m, uint16_t left, uint16_t right, bool byte, bool subtract) {
    const unsigned mask = byte ? 0xFF : 0xFFFF, sign = byte ? 0x80 : 0x8000;
    const unsigned wide = subtract ? unsigned(left) - right : unsigned(left) + right;
    const unsigned result = wide & mask;
    uint16_t flags = m.flags() & ~(emu::CF | emu::PF | emu::AF | emu::ZF | emu::SF | emu::OF);
    if (wide > mask)
        flags |= emu::CF;
    if (!result)
        flags |= emu::ZF;
    if (result & sign)
        flags |= emu::SF;
    if (!(std::popcount(uint8_t(result)) & 1))
        flags |= emu::PF;
    if ((left ^ right ^ result) & 0x10)
        flags |= emu::AF;
    if (((subtract ? left ^ right : ~(left ^ right)) & (left ^ result) & sign))
        flags |= emu::OF;
    m.flags() = flags;
}

constexpr uint16_t kProgressPointers = 0xB92D;
constexpr uint16_t kWaitFrames = 0x0000;

// Creation type; equipment menus reuse it as the selected member.

constexpr uint16_t kCreationTypes = 0xC703;
// D_3476B3[index]: the party menu's category names (10); an index past them reads what the original does
TextKey party_menu_category(uint8_t index) {
    return TextKey(uint16_t(TextKey::party_menu_category_0) + std::min<uint8_t>(index, 9));
}
TextPtr party_menu_category_text(Machine &m, uint8_t index) {
    return index < 10 ? text(m, party_menu_category(index))
                      : TextPtr{m.cs(), m.rd16(m.cs(), uint16_t(0x76b3 + index * 2))};
}
// Clamp AX and format the requested decimal width at DS:DI, with leading blanks and an FF terminator.
void format_decimal_core(Machine &m, uint16_t limit, uint16_t digits) {
    m.si() = kDigitBuffer;
    m.ax() = number_to_digits_core(m, m.ax() < limit ? m.ax() : limit, m.ds(), m.si());
    m.si() = uint16_t(kDigitBuffer + 5 - digits);
    m.cx() = uint16_t(digits - 1);
    while (m.cx()) {
        m.set_al(m.rd8(m.ds(), m.si()));
        if (m.al())
            break;
        ++m.si();
        m.set_al(0xD9);
        m.wr8(m.ds(), m.di(), m.al());
        ++m.di();
        --m.cx();
    }
    ++m.cx();
    do {
        m.set_al(uint8_t(m.rd8(m.ds(), m.si()) + 0xBB));
        m.wr8(m.ds(), m.di(), m.al());
        ++m.di();
        ++m.si();
    } while (--m.cx());
    m.set_al(0xFF);
    m.wr8(m.ds(), m.di(), m.al());
    ++m.di();
}
// Reset AL-selected history: count zero, circular index 99 and 100 FF-filled type/meat pairs.
void clear_progress_core(Machine &m) {
    const auto ax = m.ax();
    m.di() = m.rd16(m.cs(), uint16_t(kProgressPointers + m.al() * 2));
    m.wr16(m.ss(), m.di(), kWaitFrames);
    m.di() += 2;
    m.wr16(m.ss(), m.di(), 99);
    m.di() += 2;
    m.cx() = 200;
    do {
        m.wr8(m.ss(), m.di(), 0xFF);
        ++m.di();
    } while (--m.cx());
    m.ax() = ax;
}
uint16_t text_destination(Machine &m) { return screen_map_offset(m.al(), m.bx()); }
} // namespace
// Format AX as three digits, clamped to 999, with leading blanks and FF terminator at DS:DI.
void format_stat_3digit_core(Machine &m) { format_decimal_core(m, 999, 3); }
// rom: 0x347F16 (near)
// Format AX as three digits, clamped to 999, with leading blanks and FF terminator at DS:DI.
void format_stat_3digit(Machine &m) {
    format_stat_3digit_core(m);
    m.ret();
}
// Format a clamped hour (0..23) as two glyphs at DS:DI.
void format_hours_2digit_core(Machine &m) { format_decimal_core(m, 23, 2); }
// rom: 0x34804C (near)
// Format a clamped hour (0..23) as two glyphs at DS:DI.
void format_hours_2digit(Machine &m) {
    format_hours_2digit_core(m);
    m.ret();
}
// Shared conversion logic for number32_to_digits and format_gold.
// Convert DX:AX into ten decimal bytes at DS:SI; preserves BX, CX, BP and leaves the units in AX.
void number32_to_digits_core(Machine &m) {
    uint32_t number = (uint32_t(m.dx()) << 16) | m.ax();
    constexpr uint32_t places[] = {1000000000, 100000000, 10000000, 1000000, 100000, 10000, 1000, 100, 10, 1};
    // The original's last three places operate on AX alone; after the larger subtractions DX is zero.
    for (unsigned i = 0; i < 10; ++i) {
        m.wr8(m.ds(), uint16_t(m.si() + i), uint8_t(number / places[i]));
        number %= places[i];
        if (i == 8)
            m.ax() = uint16_t(number);
    }
    m.dx() = 0;
}
// rom: 0x340AA2
// Convert DX:AX into ten decimal bytes at DS:SI, preserving BX/CX/BP.
void number32_to_digits(Machine &m) {
    number32_to_digits_core(m);
    m.retf();
}
// Format the six displayed gold digits at D7EF, preserving DS/ES/SI/DI.
void format_amount_core(Machine &m, uint16_t low, uint16_t high) {
    const auto ds = m.ds(), es = m.es(), si = m.si(), di = m.di();
    m.ds() = m.es() = 0;
    m.ax() = m.rd16(0, low);
    m.dx() = m.rd16(0, high);
    m.si() = kDigitBuffer;
    number32_to_digits_core(m);
    m.si() = kDigitBuffer + 4;
    m.di() = kGoldText;
    m.cx() = 5;
    while (m.cx()) {
        m.set_al(m.rd8(0, m.si()));
        if (m.al())
            break;
        ++m.si();
        m.set_al(0xD9);
        m.wr8(0, m.di(), m.al());
        ++m.di();
        --m.cx();
    }
    ++m.cx();
    do {
        m.set_al(uint8_t(m.rd8(0, m.si()) + 0xBB));
        m.wr8(0, m.di(), m.al());
        ++m.di();
        ++m.si();
    } while (--m.cx());
    m.set_al(0xFF);
    m.wr8(0, m.di(), m.al());
    ++m.di();
    m.ds() = ds;
    m.es() = es;
    m.si() = si;
    m.di() = di;
}
void format_gold_core(Machine &m) { format_amount_core(m, kGoldLow, kGoldHigh); }
// rom: 0x348083 (near)
// Format the six displayed gold digits at D7EF, preserving DS/ES/SI/DI.
void format_gold(Machine &m) {
    format_gold_core(m);
    m.ret();
}
// Shared body of ROM 0x345E8D
// Reset one member's 100-entry circular monster history, starting at index 99.
void clear_progress_history_core(Machine &m) {
    clear_progress_core(m);
    return;
}

// rom: 0x345E8D (near)
void clear_progress_history(Machine &m) {
    clear_progress_history_core(m);
    m.ret();
}
// rom: 0x345E78
// Clear all four progress histories; AH remains the caller's high byte.
void clear_progress_histories(Machine &m) {
    for (unsigned i = 0; i < 4; ++i) {
        m.set_al(i);
        clear_progress_core(m);
    }
    m.retf();
}
// rom: 0x345E5A
// Zero the 19 event flag bytes, SS relative; leaves DI past them and CX zero.
void clear_event_flags(Machine &m) {
    m.cx() = 19;
    m.di() = kEventFlags;
    m.set_al(0);
    do {
        m.wr8(m.ss(), m.di(), 0);
        ++m.di();
    } while (--m.cx());
    m.retf();
}
// rom: 0x347722
// Restore the default dialogue speed and four party order bytes.
void reset_dialogue_party_order_call_core(Machine &m) {
    m.set_al(4);
    m.wr8(m.ss(), kDialogueSpeed, 4);
    constexpr uint8_t kDefaultPartyOrder[] = {3, 1, 8, 2};
    for (unsigned i = 0; i < 4; ++i)
        m.wr8(m.ss(), uint16_t(kPartyOrder + i), kDefaultPartyOrder[i]);
    m.set_al(31);
    m.wr8(m.ss(), kOptions, 31);
}
void reset_dialogue_party_order(Machine &m) {
    reset_dialogue_party_order_call_core(m);
    m.retf();
}
// Copy CX bytes of game state into ES:DI and append their 16-bit additive checksum. CX/DI preserved.
void copy_save_payload_core(Machine &m) {
    const auto cx = m.cx(), di = m.di();
    m.si() = kGameState;
    m.dx() = 0;
    m.set_ah(0);
    do {
        m.set_al(m.rd8(m.ds(), m.si()));
        m.wr8(m.es(), m.di(), m.al());
        m.dx() = uint16_t(m.dx() + m.ax());
        ++m.si();
        ++m.di();
    } while (--m.cx());
    m.wr16(m.es(), m.di(), m.dx());
    m.cx() = cx;
    m.di() = di;
}
// rom: 0x346A48 (near)
// Copy CX bytes of game state into ES:DI and append their 16-bit additive checksum. CX/DI preserved.
void copy_save_payload(Machine &m) {
    copy_save_payload_core(m);
    m.ret();
}
// Draw the DX-selected 2x2 icon; preserves AX/BX/CX/DX/DS, leaves SI at the selected source.
void draw_creation_icon_core(Machine &m) {
    const auto ax = m.ax(), bx = m.bx(), cx = m.cx(), dx = m.dx(), ds = m.ds();
    m.ds() = m.cs();
    m.dx() = uint16_t(m.dx() << 3);
    m.si() = uint16_t(0xB2F9 + m.dx());
    m.cx() = 0x202;
    draw_tilemap_core(m);
    m.ax() = ax;
    m.bx() = bx;
    m.cx() = cx;
    m.dx() = dx;
    m.ds() = ds;
}
// rom: 0x34B2DC (near)
// Draw the DX-selected 2x2 icon; preserves AX/BX/CX/DX/DS, leaves SI at the selected source.
void draw_creation_icon(Machine &m) {
    draw_creation_icon_core(m);
    m.ret();
}
// Draw BCD DX left aligned, suppressing leading zeroes; zero draws one digit and preserves CX.
void draw_bcd_left_core(Machine &m) {
    const auto bx = m.bx();
    uint16_t di = text_destination(m);
    m.flags() &= uint16_t(~0x400);
    m.ax() = 0x1F00;
    if (!m.dx()) {
        m.set_al(0xBB);
        m.wr16(0, di, m.ax());
    } else {
        m.cx() = 4;
        bool started = false;
        do {
            m.dx() = uint16_t((m.dx() << 4) | (m.dx() >> 12));
            m.set_al(m.dx() & 15);
            started |= m.al() != 0;
            if (started) {
                m.set_al(uint8_t(m.al() + 0xBB));
                m.wr16(0, di, m.ax());
                di = uint16_t(di + 2);
            }
        } while (--m.cx());
    }
    m.bx() = bx;
}
// rom: 0x347E68 (near)
// Draw BCD DX left aligned, suppressing leading zeroes; zero draws one digit and preserves CX.
void draw_bcd_left(Machine &m) {
    draw_bcd_left_core(m);
    m.ret();
}
// Draw CX BCD glyphs right to left, including leading zeroes, clamping overflow to 9999.
void draw_bcd_zero_padded_core(Machine &m) {
    uint16_t di = text_destination(m);
    m.flags() &= uint16_t(~0x400);
    m.ax() = m.dx();
    m.bx() = 4;
    while (!(m.ax() & 0xF000)) {
        m.ax() = uint16_t(m.ax() << 4);
        if (!--m.bx())
            break;
    }
    if (m.bx() > m.cx())
        m.dx() = 0x9999;
    m.ax() = 0x1F00;
    do {
        m.set_al(uint8_t((m.dx() & 15) + 0xBB));
        m.wr16(0, di, m.ax());
        di = uint16_t(di - 2);
        m.dx() >>= 4;
    } while (--m.cx());
}
// rom: 0x347E0E (near)
// Draw CX BCD glyphs right to left, including leading zeroes, clamping overflow to 9999.
void draw_bcd_zero_padded(Machine &m) {
    draw_bcd_zero_padded_core(m);
    m.ret();
}
// Draw the DL item name from bank 39, preserving AX and advancing BX by nine columns.
void draw_item_name_core(Machine &m) {
    const auto ax = m.ax(), bx = m.bx();
    const TextPtr name = name_record(m, NameTable::Item, uint8_t(m.dx())); // a loaded language's (text.h)
    m.ds() = name.segment;
    m.si() = name.offset;
    draw_text_core(m);
    m.ax() = ax;
    m.bx() = uint16_t(bx + 9);
}
// rom: 0x347CA9 (near)
// Draw the DL item name from bank 39, preserving AX and advancing BX by nine columns.
void draw_item_name(Machine &m) {
    draw_item_name_core(m);
    m.ret();
}
// Shared body of ROM 0x341310
// CF reports whether a requested status-menu category is available. Bit 1 always succeeds;
// bits 8..11 check the four party order bytes (zero and nine are unavailable).
void test_menu_categories_core(Machine &m) {
    const auto ax = m.ax();
    constexpr uint16_t arithmetic = emu::CF | emu::OF | emu::AF | emu::ZF | emu::SF | emu::PF;
    uint16_t flags = 0;
    bool available = (ax & 2) != 0;
    if (available)
        flags = 0;
    else {
        m.cx() = 4;
        m.si() = kPartyOrderLast;
        uint16_t mask = uint16_t(ax << 4);
        do {
            const bool selected = (mask & 0x8000) != 0;
            mask = uint16_t(mask << 1);
            uint8_t category = m.rd8(m.ss(), m.si());
            if (selected && category != 0 && category != 9) {
                const uint8_t result = uint8_t(category - 9);
                flags = uint16_t((category < 9 ? emu::CF : 0) | ((result & 0x80) ? emu::SF : 0) |
                                 (((category ^ 9 ^ result) & 0x10) ? emu::AF : 0) |
                                 (((category ^ 9) & (category ^ result) & 0x80) ? emu::OF : 0));
                unsigned parity = 0;
                for (unsigned bit = 0; bit < 8; ++bit)
                    parity += (result >> bit) & 1;
                if (!(parity & 1))
                    flags |= emu::PF;
                available = true;
                break;
            }
            --m.si();
        } while (--m.cx());
        if (!available)
            flags = emu::SF | emu::PF;
    }
    m.flags() = uint16_t((m.flags() & ~arithmetic) | flags);
    if (available)
        m.flags() |= emu::CF;
    else
        m.flags() &= uint16_t(~emu::CF);
    return;
}

// rom: 0x341310
void test_menu_categories(Machine &m) {
    test_menu_categories_core(m);
    m.retf();
}
// Human race CL=0 uses normal terminated text; other races draw at most three dark glyphs.
void draw_text_n_dark_core(Machine &m) {
    uint16_t destination = screen_map_offset(m.al(), m.bx());
    m.flags() &= uint16_t(~emu::DF);
    m.ax() = 0x1e00;
    do {
        m.set_al(m.rd8(m.ds(), m.si()));
        if (m.al() == 255)
            break;
        ++m.si();
        m.bx() = uint16_t((m.bx() & 0xff00) | uint8_t(m.bx() + 1));
        m.wr16(0, destination, m.ax());
        destination = uint16_t(destination + 2);
    } while (--m.cx());
}
// Values for the value tokens (text_tokens.h) from a member record (31 bytes at seg:record: status +0,
// HP +6, max HP +8, hearts in the low nibble of +10), as the game draws them: a set status shows its
// condition in place of max HP (the names at CS:872C by its lowest bit; dead, stone, curse and blind have
// host strings).
std::u32string member_hp(Machine &m, uint16_t seg, uint16_t record) {
    return decimal_text(m.rd16(seg, uint16_t(record + 6)), 3);
}
std::u32string member_hearts(Machine &m, uint16_t seg, uint16_t record) {
    return decimal_text(m.rd8(seg, uint16_t(record + 10)) & 15u, 2);
}
std::u32string member_maxhp(Machine &m, uint16_t seg, uint16_t record) {
    uint8_t status = m.rd8(seg, record);
    if (!status)
        return decimal_text(m.rd16(seg, uint16_t(record + 8)), 3);
    unsigned bit = 0;
    while (bit < 8 && !(status & 1))
        status >>= 1, ++bit;
    static constexpr const char *kConditions[] = {"ui_status_dead", "ui_status_stone", "ui_status_curse",
                                                  "ui_status_blind"};
    if (bit < 4 && m.text_table() && m.text_table()->proportional())
        return host_text(m.wsc(), kConditions[bit]);
    return ram_text(m, m.cs(), m.rd16(m.cs(), uint16_t(0x872c + bit * 2)), 3);
}
void draw_race_stat_core(Machine &m) {
    if (uint8_t(m.cx()) == 0)
        draw_text_core(m);
    else {
        m.cx() = uint16_t((m.cx() & 0xff00) | 3);
        draw_text_n_dark_core(m);
    }
}
// rom: 0x34873C (near)
// Human race CL=0 uses normal terminated text; other races draw at most three dark glyphs.
void draw_race_stat(Machine &m) {
    draw_race_stat_core(m);
    m.ret();
}
// Draw the AX-selected eight-byte character-creation label. All registers/segments preserved.
void draw_creation_race_label_core(Machine &m) {
    const auto ax = m.ax(), bx = m.bx(), cx = m.cx(), dx = m.dx(), bp = m.bp(), si = m.si(), di = m.di(),
               ds = m.ds(), es = m.es();
    const TextKey key = TextKey(uint16_t(TextKey::creation_race_label_0) + std::min<uint8_t>(m.al(), 24));
    const TextPtr text_ptr = m.al() < 25 ? text(m, key) // D_34BD4A + 8 * AL: 25 race labels
                                         : TextPtr{m.cs(), uint16_t(0xBD4A + m.al() * 8)};
    m.ds() = text_ptr.segment;
    m.dx() = uint16_t((m.dx() & 0xFF00) | 8);
    m.si() = text_ptr.offset;
    m.ax() = 1;
    draw_text_core(m);
    text_done(m, key);
    m.ax() = ax;
    m.bx() = bx;
    m.cx() = cx;
    m.dx() = dx;
    m.bp() = bp;
    m.si() = si;
    m.di() = di;
    m.ds() = ds;
    m.es() = es;
}
// rom: 0x345D22 (near)
// Draw the AX-selected eight-byte character-creation label. All registers/segments preserved.
void draw_creation_race_label(Machine &m) {
    draw_creation_race_label_core(m);
    m.ret();
}
namespace {
// Register saves are local values: no synthetic machine call/return frames are created.
struct MenuRegisters {
    uint16_t ax, bx, cx, dx, bp, si, di, ds, es, ss;
    explicit MenuRegisters(Machine &m)
        : ax(m.ax()), bx(m.bx()), cx(m.cx()), dx(m.dx()), bp(m.bp()), si(m.si()), di(m.di()), ds(m.ds()),
          es(m.es()), ss(m.ss()) {}
    void restore(Machine &m) const {
        m.ax() = ax;
        m.bx() = bx;
        m.cx() = cx;
        m.dx() = dx;
        m.bp() = bp;
        m.si() = si;
        m.di() = di;
        m.ds() = ds;
        m.es() = es;
        m.ss() = ss;
    }
};
void move_column(Machine &m, unsigned columns) {
    m.bx() = uint16_t((m.bx() & 0xff00) | uint8_t(m.bx() + columns));
}
void move_row(Machine &m, unsigned rows) {
    m.bx() = uint16_t((m.bx() & 0xff) | (uint16_t(uint8_t((m.bx() >> 8) + rows)) << 8));
}
// Load one member's walking tiles and palette into the menu's numbered graphics slot.
void load_member_graphics(Machine &m, uint16_t member, uint16_t slot, bool preserve_bx_dx) {
    const auto ds = m.ds(), bx = m.bx(), dx = m.dx();
    m.set_al(m.rd8(m.ss(), uint16_t(member + 5)));
    char_type_to_gfx_index_core(m);
    const auto graphic = m.ax();
    if (preserve_bx_dx) {
        m.bx() = bx;
        m.dx() = dx;
    }
    m.bp() = uint16_t(uint8_t(graphic) * 4);
    m.es() = 0x9000;
    m.ds() = m.rd16(m.es(), uint16_t(m.bp() + 0x3f70));
    m.ax() = uint16_t(slot * 4);
    m.cx() = 4;
    m.si() = m.rd16(m.es(), uint16_t(m.bp() + 0x40b0));
    const auto race = m.rd8(m.ss(), member);
    if (race & 1)
        m.si() = uint16_t(m.si() + 0x880);
    else if (race & 12)
        m.si() = uint16_t(m.si() + 0x800);
    copy_tiles_core(m);
    if (preserve_bx_dx) {
        m.bx() = bx;
        m.dx() = dx;
    }
    m.ax() = uint16_t((graphic >> 8) * 32);
    m.si() = uint16_t(m.rd16(m.es(), uint16_t(m.bp() + 0x40b2)) + m.ax());
    m.di() = kPaletteBuffer;
    m.cx() = 1;
    m.ax() = uint16_t(slot + 8);
    if (m.rd8(m.ss(), member) & 2)
        m.si() = uint16_t(m.rd16(m.es(), uint16_t(m.bp() + 0x40b2)) + 0xc0);
    copy_palettes_core(m);
    if (preserve_bx_dx)
        m.bx() = bx;
    m.ds() = ds;
}
void draw_item_icon_core(Machine &m) {
    const auto ax = m.ax(), bx = m.bx(), dx = m.dx();
    m.ds() = 0x9000;
    m.dx() = uint8_t(dx);
    m.si() = uint16_t(0x6ee0 + m.dx());
    m.ax() = m.rd8(m.ds(), m.si());
    m.cx() = 0x101;
    if (m.al() == 0xff) {
        m.ax() = ax;
        m.dx() = 0x1fd9;
        fill_screen_rect_core(m);
    } else {
        m.ds() = 0x4000;
        m.ax() = uint16_t(m.ax() * 2);
        m.si() = uint16_t(kItemIconTiles + m.ax());
        m.ax() = ax;
        draw_tilemap_core(m);
    }
    m.dx() = dx;
    m.bx() = uint16_t(bx + 1);
    m.ax() = ax;
}
void draw_item_count_core(Machine &m) {
    if ((m.dx() >> 8) == 0xfe) {
        const TextPtr text_ptr = text(m, TextKey::draw_item_count_1);
        m.ds() = text_ptr.segment;
        m.si() = text_ptr.offset;
        move_column(m, 255);
        draw_text_core(m);
        text_done(m, TextKey::draw_item_count_1);
    } else {
        m.dx() = m.dx() >> 8;
        decimal_to_bcd_clamped_core(m);
        m.cx() = 2;
        draw_bcd_number_core(m);
    }
}
void draw_item_entry_core(Machine &m) {
    const auto ds = m.ds();
    misc_arithmetic_flags(m, m.dx() >> 8, 0xFF, true, true);
    if ((m.dx() >> 8) != 0xFF) {
        misc_arithmetic_flags(m, uint8_t(m.dx()), 0xFC, true, true);
        if (uint8_t(m.dx()) < 0xFC) {
            draw_item_icon_core(m);
            draw_item_name_core(m);
            draw_item_count_core(m);
        }
    }
    m.ds() = ds;
}
void load_save_leader_sprite_core(Machine &m) {
    m.ax() = uint16_t(m.rd8(m.ss(), uint16_t(m.bx() + 0xe3)) * 31);
    m.cx() = 31;
    m.bx() = uint16_t(m.bx() + m.ax());
    load_member_graphics(m, m.bx(), m.dx(), true);
}
void load_creation_race_sprite_core(Machine &m) {
    const MenuRegisters saved(m);
    m.cx() = uint16_t((m.cx() & 0xff00) | 6);
    m.ax() = uint16_t(m.al() * 6);
    char_type_to_gfx_index_core(m);
    m.bp() = uint16_t(m.al() * 4);
    m.es() = 0x9000;
    m.ds() = m.rd16(m.es(), uint16_t(m.bp() + 0x3f70));
    m.ax() = uint16_t(m.dx() * 4 + 16);
    m.cx() = 4;
    m.si() = m.rd16(m.es(), uint16_t(m.bp() + 0x40b0));
    copy_tiles_core(m);
    m.dx() = saved.dx;
    m.di() = kPaletteBuffer;
    m.ax() = uint16_t(m.dx() + 4);
    m.cx() = 1;
    m.si() = uint16_t(m.rd16(m.es(), uint16_t(m.bp() + 0x40b2)) + 0xc0);
    copy_palettes_core(m);
    saved.restore(m);
}
void draw_save_party_preview_core(Machine &m) {
    const auto ax = m.ax(), bx = m.bx();
    m.di() = kSaveScratch;
    m.set_al(save_slot_for_row(m, m.al())); // Save Slots 9: AL is the selected row
    load_save_slot_core(m);
    const bool valid = !(m.flags() & emu::CF);
    for (unsigned slot = 3; slot < 6; ++slot) {
        m.ax() = uint16_t(slot * 4);
        m.bx() = 0xe0;
        m.dx() = 0x2000;
        set_sprite_16x16_core(m);
    }
    if (!valid)
        return;
    m.ax() = ax;
    m.bx() = bx;
    m.si() = kSaveScratch;
    m.set_al(m.rd8(m.ss(), uint16_t(m.si() + 0xe3)));
    m.wr8(m.ss(), kCreationLeader, m.al());
    m.ax() = 0;
    m.cx() = 4;
    m.dx() = 3;
    do {
        if (m.al() != m.rd8(m.ss(), kCreationLeader) && m.rd8(m.ss(), m.si()) != 0xff) {
            const auto saved_ax = m.ax(), saved_bx = m.bx(), saved_cx = m.cx(), saved_dx = m.dx(),
                       saved_si = m.si();
            m.bx() = saved_si;
            load_member_graphics(m, m.bx(), m.dx(), true);
            m.ax() = saved_ax;
            m.bx() = saved_bx;
            m.dx() = saved_dx;
            m.cx() = uint16_t(saved_dx << 9);
            m.dx() = uint16_t((saved_dx << 2) + 0x2000 + m.cx());
            m.ax() = uint16_t(saved_dx * 4);
            move_column(m, 1);
            m.bx() = uint16_t((uint16_t(uint8_t((m.bx() >> 8) * 8 + 4)) << 8) | uint8_t(m.bx() * 8));
            set_sprite_16x16_core(m);
            m.ax() = saved_ax;
            m.bx() = saved_bx;
            m.cx() = saved_cx;
            m.dx() = saved_dx;
            m.si() = saved_si;
            move_column(m, 2);
            ++m.dx();
        }
        ++m.ax();
        m.si() = uint16_t(m.si() + 31);
    } while (--m.cx());
}
} // namespace
// rom: 0x347C67 (near)
// Draw an item's one-cell icon, or a blank for the FF icon sentinel; advance BX one column.
void draw_item_icon(Machine &m) {
    draw_item_icon_core(m);
    m.ret();
}
// rom: 0x347CCD (near)
// Draw quantity DH as two BCD cells, or the infinite marker for FE.
void draw_item_count(Machine &m) {
    draw_item_count_core(m);
    m.ret();
}
// rom: 0x347C51 (near)
// Draw icon/name/count for a valid item, preserving DS.
void draw_item_entry_call_core(Machine &m) { draw_item_entry_core(m); }
void draw_item_entry(Machine &m) {
    draw_item_entry_call_core(m);
    m.ret();
}
// Draw icon/name for a valid item, preserving DS.
void draw_item_without_count_core(Machine &m) {
    auto ds = m.ds();
    if ((m.dx() >> 8) != 0xff && uint8_t(m.dx()) < 0xfc) {
        draw_item_icon_core(m);
        draw_item_name_core(m);
    }
    m.ds() = ds;
}
// rom: 0x347CF3 (near)
// Draw icon/name for a valid item, preserving DS.
void draw_item_without_count(Machine &m) {
    draw_item_without_count_core(m);
    m.ret();
}
// Draw CX two-byte item records in successive rows. DS can be changed by the item renderer.
void draw_item_entry_rows_core(Machine &m) {
    do {
        m.dx() = m.rd16(m.ds(), m.si());
        auto saved_ax = m.ax(), saved_bx = m.bx(), saved_cx = m.cx(), saved_si = m.si();
        draw_item_entry_core(m);
        m.ax() = saved_ax;
        m.bx() = saved_bx;
        m.cx() = saved_cx;
        m.si() = saved_si;
        move_row(m, 1);
        misc_arithmetic_flags(m, m.si(), 2, false, false);
        m.si() = uint16_t(m.si() + 2);
    } while (--m.cx());
}
// Shared body of ROM 0x343B07
// Draw CX two-byte item records in successive rows. DS can be changed by the item renderer.

void draw_item_entry_rows_entry_core(Machine &m) { draw_item_entry_rows_core(m); }

// rom: 0x343B07 (near)
void draw_item_entry_rows(Machine &m) {
    draw_item_entry_rows_entry_core(m);
    m.ret();
}
// rom: 0x346913 (near)
// Load the saved party leader's menu sprite tiles and palette into slot DX.
void load_save_leader_sprite(Machine &m) {
    load_save_leader_sprite_core(m);
    m.ret();
}
// Load all four party members' menu tiles and palettes into slots 0..3.
void load_party_menu_sprites_core(Machine &m) {
    m.ax() = 0;
    m.ds() = 0;
    for (unsigned i = 0; i < 4; ++i)
        load_member_graphics(m, uint16_t(kGameState + i * 31), uint16_t(i), false);
}
// rom: 0x341793 (near)
// Load all four party members' menu tiles and palettes into slots 0..3.
void load_party_menu_sprites(Machine &m) {
    load_party_menu_sprites_core(m);
    m.ret();
}
// rom: 0x345D3E (near)
// Load race-select graphics in slot DX+4; all general and segment registers preserved.
void load_creation_race_sprite(Machine &m) {
    load_creation_race_sprite_core(m);
    m.ret();
}
// Draw the race sprite/label and six flag-gated emblems, preserving registers.
void draw_creation_race_row_core(Machine &m) {
    const MenuRegisters saved(m);
    load_creation_race_sprite_core(m);
    draw_creation_race_label_core(m);
    move_row(m, 1);
    move_column(m, 2);
    m.cx() = uint16_t((m.cx() & 0xff00) | 6);
    m.ax() = uint16_t(m.al() * 6);
    m.cx() = 6;
    do {
        draw_flag_gated_icon_core(m);
        move_column(m, 2);
        ++m.ax();
    } while (--m.cx());
    saved.restore(m);
}
// rom: 0x345CFD (near)
// Draw the race sprite/label and six flag-gated emblems, preserving registers.
void draw_creation_race_row(Machine &m) {
    draw_creation_race_row_core(m);
    m.ret();
}
// rom: 0x346553 (near)
// Verify slot AL and draw its three nonleader party sprites, hiding unused slots.
void draw_save_party_preview(Machine &m) {
    draw_save_party_preview_core(m);
    m.ret();
}
// Shared body of ROM 0x34653B
// Draw the selected slot's party preview at its menu row.
void draw_selected_save_party_core(Machine &m) {
    m.ax() = m.rd8(m.ss(), kSaveSlotSelection);
    m.cx() = 5;
    m.bx() = uint16_t((uint16_t(uint8_t(m.al() * 5 + 3)) << 8) | 6);
    draw_save_party_preview_core(m);
    return;
}

// rom: 0x34653B (near)
void draw_selected_save_party(Machine &m) {
    draw_selected_save_party_core(m);
    m.ret();
}
// Shared body of ROM 0x34B730
// Redraw the entered name and highlighted insertion position (4 clamps to 3).
void draw_name_entry_caret_core(Machine &m) {
    if (auto *session = Session::find(m.wsc()); session && session->player_names().enabled) {
        draw_player_name_editor(m);
        return;
    }
    m.ds() = m.ss();
    m.si() = kNameText;
    m.ax() = 0;
    m.bx() = 0x118;
    m.cx() = 4;
    draw_text_n_white_core(m);
    m.ds() = m.cs();
    m.si() = 0xbe80;
    m.ax() = 1;
    m.bx() = 0x118;
    m.cx() = 0x105;
    m.dx() = 0x1ff;
    fill_screen_rect_core(m);
    m.ds() = m.ss();
    m.si() = kTabTransferCount;
    auto col = m.rd8(m.ss(), kNameInsertionIndex);
    if (col == 4)
        col = 3;
    m.bx() = uint16_t(0x100 | uint8_t(col + 0x18));
    m.ax() = 1;
    m.cx() = 1;
    draw_text_n_white_core(m);
    return;
}

// rom: 0x34B730 (near)
void draw_name_entry_caret(Machine &m) {
    draw_name_entry_caret_core(m);
    m.ret();
}

namespace {
void menu_text(Machine &m, uint16_t segment, uint16_t ax, uint16_t bx, uint16_t si) {
    m.ds() = segment;
    m.ax() = ax;
    m.bx() = bx;
    m.si() = si;
    draw_text_core(m);
}
void menu_text(Machine &m, TextKey key, uint16_t ax, uint16_t bx) {
    const TextPtr text_ptr = text(m, key);
    menu_text(m, text_ptr.segment, ax, bx, text_ptr.offset);
    text_done(m, key);
}
// A label with the formatted gold (kGoldText) drawn over its gap; a language with {gold} (text_tokens.h)
// gets the amount in its string instead.
void gold_text(Machine &m, TextKey key, uint16_t ax, uint16_t bx) {
    {
        TextValues values;
        values.set(U"gold", ram_text(m, 0, kGoldText));
        menu_text(m, key, ax, bx);
    }
    if (!has_value_tokens(m, key))
        menu_text(m, 0, ax, bx, kGoldText);
}
// Build party/status text and load the selected menu palette and graphics.
void prepare_party_menu_core(Machine &m) {
    m.ax() = 0;
    m.ds() = m.es() = 0;
    for (unsigned i = 0; i < 4; ++i) {
        m.si() = uint16_t(kMemberName + 31 * i);
        m.di() = uint16_t(kPartyMenuText + 51 * i);
        m.cx() = 4;
        copy_bytes_terminated_core(m);
    }
    for (unsigned stat = 0; stat < 2; ++stat)
        for (unsigned i = 0; i < 4; ++i) {
            m.ax() = m.rd16(m.ss(), uint16_t(kMemberHp + stat * 2 + 31 * i));
            m.di() = uint16_t(kMeatSourceType + stat * 4 + 51 * i);
            format_stat_3digit_core(m);
        }
    for (unsigned i = 0; i < 4; ++i) {
        m.set_al(m.rd8(m.ss(), uint16_t(kGameState + 31 * i)));
        m.di() = uint16_t(kMemberRaceText + 51 * i);
        write_race_abbreviation_core(m);
    }
    for (unsigned i = 0; i < 4; ++i) {
        m.ax() = m.rd8(m.ss(), uint16_t(kMemberEquipmentFlags + 31 * i)) & 15;
        m.di() = uint16_t(kMemberEquipmentText + 51 * i);
        format_stat_1digit_core(m);
    }
    format_gold_core(m);
    for (unsigned i = 0; i < 4; ++i) {
        auto ds = m.ds();
        m.set_al(m.rd8(m.ss(), uint16_t(kMemberType + 31 * i)));
        lookup_name_record_core(m);
        m.cx() = 8;
        m.di() = uint16_t(kMemberTypeText + 51 * i);
        copy_bytes_terminated_core(m);
        m.ds() = ds;
    }
    for (unsigned stat = 0; stat < 4; ++stat)
        for (unsigned i = 0; i < 4; ++i) {
            m.ax() = m.rd8(m.ss(), uint16_t(kMemberStrength + stat + 31 * i));
            m.di() = uint16_t(kMemberStatText + stat * 4 + 51 * i);
            format_stat_2digit_core(m);
        }
    for (unsigned i = 0; i < 2; ++i) {
        m.ax() = m.rd8(m.ss(), uint16_t(kTowerCounter + i));
        m.di() = uint16_t(kTowerCounterText + i * 4);
        format_hours_2digit_core(m);
    }
    draw_play_time_core(m);
    m.ds() = 0x9000;
    m.ax() = 0x100;
    m.cx() = 0x100;
    m.si() = 0x80;
    copy_tiles_core(m);
    m.ax() = 15;
    m.cx() = 1;
    m.si() = 0;
    m.di() = kPaletteBuffer;
    copy_palettes_core(m);
    for (unsigned i = 0; i < 2; ++i) {
        m.ax() = uint16_t(i * 4);
        m.bx() = i ? 0xe0 : 0x898;
        m.dx() = 0x3ffb;
        set_sprite_16x16_core(m);
    }
    m.ds() = 0x9000;
    constexpr uint16_t tiles[][3] = {{0x80, 0x20, 0x2480}, {0xa0, 0x2c, 0x2ac0}, {0xcc, 0x10, 0x3d46}};
    for (auto &t : tiles) {
        m.ax() = t[0];
        m.cx() = t[1];
        m.si() = t[2];
        copy_tiles_core(m);
    }
    m.ax() = 13;
    m.cx() = 2;
    m.si() = 0x3d06;
    m.di() = kPaletteBuffer;
    copy_palettes_core(m);
    auto ds = m.ds();
    m.ds() = 0x4000;
    m.ax() = 0xe4;
    m.cx() = 12;
    m.si() = kMenuBackgroundPalettes;
    copy_tiles_core(m);
    m.ds() = ds;
    load_party_menu_sprites_core(m);
    for (unsigned i = 0; i < 2; ++i) {
        ds = m.ds();
        m.ds() = m.cs();
        m.ax() = uint16_t(0x1f3 + i);
        m.cx() = 1;
        m.si() = uint16_t(kPartyMenuPalettes + i * 32);
        copy_tiles_core(m);
        m.ds() = ds;
    }
}
void draw_save_slot_summary_core(Machine &m) {
    m.ds() = m.cs();
    const auto ax = m.ax(), bx = m.bx();
    const TextKey slot_label_key = ax == 2   ? TextKey::save_slot_label_3
                                   : ax == 1 ? TextKey::draw_save_slot_summary_2
                                             : TextKey::draw_save_slot_summary_1;
    const TextPtr text_ptr = text(m, slot_label_key);
    m.ds() = text_ptr.segment;
    m.si() = text_ptr.offset;
    m.ax() = 1;
    draw_text_core(m);
    text_done(m, slot_label_key);
    m.ax() = ax;
    if (const SaveSlots *slots = extended_save_slots(m)) { // Save Slots 9 / AUTO: row ax shows another slot
        const int slot = slots->slot_for_row(ax);
        if (slot == SaveSlots::kAuto) { // no number; "Autosave" (m5x7) right of the leader's sprite
            m.wr16(0, screen_map_offset(1, bx), uint16_t(0x1F00 | 0xD9));
            draw_text_pixels(m, screen_map_offset(1, uint16_t(bx + 5)), 0x1F00,
                             host_text(m.wsc(), "ui_save_autosave"), 6);
        } else // its number in place of the row's: one digit tile (0xBB + n) where draw_text put it
            m.wr16(0, screen_map_offset(1, bx), uint16_t(0x1F00 | (0xBC + slot)));
        m.ax() = uint16_t(slot);
    }
    m.bx() = bx;
    m.di() = kSaveScratch;
    load_save_slot_core(m);
    if (m.flags() & emu::CF) {
        m.bx() = uint16_t(bx + 0x20a);
        m.ax() = 0;
        {
            const TextPtr text_ptr = text(m, TextKey::draw_save_slot_summary_3);
            m.ds() = text_ptr.segment;
            m.si() = text_ptr.offset;
            draw_text_core(m);
            text_done(m, TextKey::draw_save_slot_summary_3);
        }
        m.ax() = uint16_t(ax * 4);
        m.bx() = 0xe0;
        m.dx() = uint16_t(ax + 0x2000);
        set_sprite_16x16_core(m);
        m.ax() = ax;
        m.bx() = bx;
        return;
    }
    m.dx() = ax;
    m.bx() = kSaveScratch;
    load_save_leader_sprite_core(m);
    m.ax() = uint16_t(ax * 4);
    m.bx() = bx;
    move_column(m, 1);
    m.bx() = uint16_t((uint16_t(uint8_t((m.bx() >> 8) * 8 + 4)) << 8) | uint8_t(m.bx() * 8));
    m.cx() = uint16_t(ax << 9);
    m.dx() = uint16_t((ax << 2) + 0x2000 + m.cx());
    set_sprite_16x16_core(m);
    auto base = [&](unsigned row, unsigned col, uint16_t segment, uint16_t style) {
        m.ax() = style;
        m.bx() = bx;
        move_row(m, row);
        move_column(m, col);
        m.ds() = segment;
    };
    // a language with value tokens (text_tokens.h) gets the numbers in its strings instead
    const uint16_t leader = uint16_t(kSaveScratch + m.rd8(m.ss(), kSavedLeader) * 31);
    {
        TextValues values;
        values.set(U"floor", decimal_text(m.rd8(m.ss(), kSavedTowerProgress), 2));
        const TextPtr text_ptr = text(m, TextKey::draw_save_slot_summary_4);
        base(0, 13, text_ptr.segment, 1);
        m.si() = text_ptr.offset;
        draw_text_core(m);
        text_done(m, TextKey::draw_save_slot_summary_4);
    }
    base(0, 21, m.cs(), 1);
    m.si() = kSaveScratch;
    if (!has_value_tokens(m, TextKey::draw_save_slot_summary_4)) {
        m.dx() = m.rd8(m.ss(), kSavedTowerProgress);
        decimal_to_bcd_clamped_core(m);
        m.cx() = 2;
        draw_bcd_number_core(m);
    }
    {
        TextValues values;
        values.set(U"topfloor", decimal_text(m.rd8(m.ss(), kSavedTowerCounter), 2));
        const TextPtr text_ptr = text(m, TextKey::draw_save_slot_summary_5);
        base(1, 13, text_ptr.segment, 1);
        m.si() = text_ptr.offset;
        draw_text_core(m);
        text_done(m, TextKey::draw_save_slot_summary_5);
    }
    base(1, 14, m.cs(), 1);
    m.si() = kSaveScratch;
    if (!has_value_tokens(m, TextKey::draw_save_slot_summary_5)) {
        m.dx() = m.rd8(m.ss(), kSavedTowerCounter);
        decimal_to_bcd_clamped_core(m);
        m.cx() = 2;
        draw_bcd_number_core(m);
    }
    base(3, 2, m.ss(), 0);
    m.si() = kSaveScratch;
    m.ax() = uint16_t(m.rd8(m.ss(), kSavedLeader) * 31);
    m.cx() = 31;
    m.si() = uint16_t(m.si() + m.ax() + 1);
    m.cx() = 4;
    m.ax() = 0;
    draw_text_n_white_core(m);
    {
        TextValues values;
        values.set(U"hp", member_hp(m, m.ss(), leader));
        const TextPtr text_ptr = text(m, TextKey::draw_save_slot_summary_6);
        base(3, 7, text_ptr.segment, 0);
        m.si() = text_ptr.offset;
        m.cx() = 5;
        draw_text_n_white_core(m);
        text_done(m, TextKey::draw_save_slot_summary_6);
    }
    base(3, 11, m.ss(), 0);
    m.si() = kSaveScratch;
    m.ax() = uint16_t(m.rd8(m.ss(), kSavedLeader) * 31);
    m.cx() = 31;
    m.si() = uint16_t(m.si() + m.ax() + 6);
    if (!has_value_tokens(m, TextKey::draw_save_slot_summary_6)) {
        m.dx() = m.rd16(m.ss(), m.si());
        decimal_to_bcd_clamped_core(m);
        m.cx() = 3;
        m.ax() = 0;
        draw_bcd_number_core(m);
    }
    {
        TextValues values;
        values.set(U"hearts", member_hearts(m, m.ss(), leader));
        const TextPtr text_ptr = text(m, TextKey::draw_save_slot_summary_7);
        base(3, 13, text_ptr.segment, 0);
        m.si() = text_ptr.offset;
        m.cx() = 2;
        draw_text_n_white_core(m);
        text_done(m, TextKey::draw_save_slot_summary_7);
    }
    base(3, 15, m.ss(), 0);
    m.si() = kSaveScratch;
    m.ax() = uint16_t(m.rd8(m.ss(), kSavedLeader) * 31);
    m.cx() = 31;
    m.si() = uint16_t(m.si() + m.ax() + 10);
    if (!has_value_tokens(m, TextKey::draw_save_slot_summary_7)) {
        m.dx() = m.rd8(m.ds(), m.si()) & 15;
        decimal_to_bcd_clamped_core(m);
        m.ax() = 0;
        draw_bcd_left_core(m);
    }
    {
        const TextPtr text_ptr = text(m, TextKey::draw_save_slot_summary_8);
        base(3, 17, text_ptr.segment, 0);
        m.si() = text_ptr.offset;
        m.cx() = 8;
        draw_text_n_white_core(m);
        text_done(m, TextKey::draw_save_slot_summary_8);
    }
    m.ds() = m.ss();
    m.si() = kSaveScratch;
    m.ax() = m.rd16(m.ss(), kSavedClockLow);
    m.dx() = m.rd16(m.ss(), kSavedClockHigh);
    play_time_to_minutes_seconds_core(m);
    m.wr16(m.ss(), kSaveMinutes, m.ax());
    base(3, 21, m.ss(), 0);
    m.dx() = m.rd8(m.ss(), kSaveMinutes);
    decimal_to_bcd_clamped_core(m);
    m.ax() = 0;
    m.cx() = 2;
    draw_bcd_number_core(m);
    base(3, 24, m.ss(), 0);
    m.dx() = m.rd8(m.ss(), kSaveSeconds);
    decimal_to_bcd_clamped_core(m);
    m.ax() = 0;
    m.cx() = 2;
    draw_bcd_zero_padded_core(m);
    m.ax() = ax;
    m.bx() = bx;
    m.si() = kSaveScratch;
    if (m.rd8(m.ss(), kSavedMarker)) {
        base(3, 0, m.cs(), 0);
        m.si() = 0x6912;
        m.cx() = 1;
        draw_text_n_white_core(m);
        m.ax() = ax;
        m.bx() = bx;
    }
}
void draw_save_slot_summaries_core(Machine &m) {
    for (unsigned i = 0; i < 3; ++i) {
        m.ax() = uint16_t(i);
        m.bx() = uint16_t(0x303 + i * 0x500);
        draw_save_slot_summary_core(m);
    }
}
} // namespace
// Shared body of ROM 0x3414C1
// Prepare party names, stats, gold/time strings and all menu graphics.

void prepare_party_menu_entry_core(Machine &m) { prepare_party_menu_core(m); }

// rom: 0x3414C1 (near)
void prepare_party_menu(Machine &m) {
    prepare_party_menu_entry_core(m);
    m.ret();
}
// rom: 0x3466C6 (near)
// Verify slot AX and render its save summary, leader, location, stats and elapsed time.
void draw_save_slot_summary(Machine &m) {
    draw_save_slot_summary_core(m);
    m.ret();
}
// rom: 0x3466AA (near)
// Draw all three save-slot summaries.
void draw_save_slot_summaries(Machine &m) {
    draw_save_slot_summaries_core(m);
    m.ret();
}
// rom: 0x3461FF (near)
// Clear cursor/party sprites, draw the save-menu frames and three summaries; preserve registers.
void draw_save_menu_core(Machine &m) {
    const MenuRegisters saved(m);
    m.ax() = 0;
    m.ds() = 0;
    for (unsigned i = 0; i < 8; ++i) {
        m.ax() = uint16_t(i * 4);
        m.bx() = 0xe0;
        m.dx() = 0x3ffb;
        set_sprite_16x16_core(m);
    }
    constexpr uint16_t frames[][3] = {
        {0, 1, 0x121c}, {0, 1, 0x0d1c}, {0, 1, 0x081c}, {0, 1, 0x0307}, {0, 8, 0x0315}};
    for (unsigned i = 0; i < 5; ++i) {
        m.ax() = frames[i][0];
        m.bx() = frames[i][1];
        m.cx() = frames[i][2];
        draw_window_core(m);
        if (i == 0) {
            m.ax() = 1;
            m.bx() = 1;
            m.cx() = 0x21c;
            m.dx() = 0x1ff;
            fill_screen_rect_core(m);
        }
    }
    menu_text(m, TextKey::draw_save_menu_1, 0, 0x103);
    menu_text(m, TextKey::draw_save_menu_2, 1, 9);
    menu_text(m, TextKey::draw_save_menu_3, 1, 0x110);
    draw_save_slot_summaries_core(m);
    saved.restore(m);
    return;
}
void draw_save_menu(Machine &m) {
    draw_save_menu_core(m);
    m.ret();
}
// Shared body of ROM 0x341A9F
// Draw the party overview frames, sprites and prepared stat strings; preserve registers.
constexpr unsigned kConfigTab =
    5; // the party menu's tabs: Item, Ability, Equip, Order, Gallery, Config, Save
void draw_party_overview_core(Machine &m) {
    const MenuRegisters saved(m);
    m.ax() = 1;
    m.bx() = 0;
    m.cx() = 0x2020;
    m.dx() = 0x1ff;
    fill_screen_rect_core(m);
    m.ax() = 0;
    m.ds() = 0;
    for (unsigned i = 0; i < 4; ++i) {
        m.ax() = uint16_t(8 + i * 4);
        m.bx() = 0xe0;
        m.dx() = uint16_t(i * 0x204);
        set_sprite_16x16_core(m);
    }
    for (unsigned i = 0; i < 4; ++i)
        if (m.rd8(m.ss(), uint16_t(kGameState + i * 31)) != 0xff) {
            m.ax() = uint16_t(8 + i * 4);
            m.bx() = uint16_t(0x1010 + i * 0x1400);
            m.dx() = uint16_t(i * 0x204);
            set_sprite_16x16_core(m);
        }
    // (without the Config tab, hooks::VB::ConfigTab: the tab window a row shorter)
    const bool config_tab = hooks::should(hooks::VB::ConfigTab, m);
    constexpr uint16_t frames[][3] = {{0, 1, 0x121c}, {1, 0xc03, 0x40f}, {0, 0x214, 0x907}};
    for (auto &f : frames) {
        m.ax() = f[0];
        m.bx() = f[1];
        m.cx() = f[2] == 0x907 && !config_tab ? 0x807 : f[2];
        draw_window_core(m);
    }
    constexpr uint16_t rows[] = {2, 4, 7, 9};
    constexpr TextKey kPartyOverviewLabel1[] = {
        TextKey::draw_party_overview_1, TextKey::draw_party_overview_3, TextKey::draw_party_overview_5,
        TextKey::draw_party_overview_7};
    constexpr TextKey kPartyOverviewLabel2[] = {
        TextKey::draw_party_overview_2, TextKey::draw_party_overview_4, TextKey::draw_party_overview_6,
        TextKey::draw_party_overview_8};
    for (unsigned i = 0; i < 4; ++i) {
        if (m.rd8(m.ss(), uint16_t(kGameState + i * 31)) == 0xff)
            continue;
        uint16_t row = rows[i], style = i & 1, buffer = uint16_t(kPartyMenuText + i * 51);
        // a language with value tokens (text_tokens.h) gets the numbers in its string instead
        const bool hp_tokens = has_value_tokens(m, kPartyOverviewLabel1[i]);
        const bool heart_tokens = has_value_tokens(m, kPartyOverviewLabel2[i]);
        {
            TextValues values;
            if (hp_tokens) {
                values.set(U"hp", ram_text(m, 0, uint16_t(buffer + 24)));
                values.set(U"maxhp", ram_text(m, 0, uint16_t(buffer + 28)));
            }
            menu_text(m, kPartyOverviewLabel1[i], style, uint16_t(((row + 1) << 8) | 5));
        }
        {
            TextValues values;
            if (heart_tokens)
                values.set(U"hearts", ram_text(m, 0, uint16_t(buffer + 49)));
            menu_text(m, kPartyOverviewLabel2[i], style, uint16_t(((row + 1) << 8) | 15));
        }
        menu_text(m, 0, style, uint16_t((row << 8) | 5), buffer);
        menu_text(m, 0, style, uint16_t((row << 8) | 10), uint16_t(buffer + 8));
        if (!hp_tokens) {
            menu_text(m, 0, style, uint16_t(((row + 1) << 8) | 7), uint16_t(buffer + 24));
            m.bx() = uint16_t(((row + 1) << 8) | 11);
            m.ax() = style;
            m.si() = uint16_t(buffer + 28);
            m.cx() = uint16_t((m.cx() & 0xff00) | m.rd8(m.ss(), uint16_t(kGameState + i * 31)));
            draw_race_stat_core(m);
        }
        if (!heart_tokens)
            menu_text(m, 0, style, uint16_t(((row + 1) << 8) | 17), uint16_t(buffer + 49));
    }
    constexpr TextKey labels[] = {TextKey::draw_party_overview_9,  TextKey::draw_party_overview_10,
                                  TextKey::draw_party_overview_11, TextKey::draw_party_overview_12,
                                  TextKey::draw_party_overview_13, TextKey::draw_party_overview_14,
                                  TextKey::draw_party_overview_15};
    for (unsigned i = 0, row = 3; i < 7; ++i)
        if (config_tab || i != kConfigTab)
            menu_text(m, labels[i], 0, uint16_t((row++ << 8) | 21));
    gold_text(m, TextKey::draw_party_overview_16, 0, 0xf13);
    menu_text(m, TextKey::draw_party_overview_17, 0, 0xd13);
    menu_text(m, 0, 0, 0xd16, kPlayMinutesText);
    menu_text(m, 0, 0, 0xd19, kPlaySecondsText);
    const bool floor_tokens = has_value_tokens(m, TextKey::draw_party_overview_18);
    const bool top_tokens = has_value_tokens(m, TextKey::draw_party_overview_19);
    {
        TextValues values;
        if (floor_tokens || top_tokens) {
            values.set(U"floor", ram_text(m, 0, kTowerProgressText));
            values.set(U"topfloor", ram_text(m, 0, kTowerCounterText));
        }
        menu_text(m, TextKey::draw_party_overview_18, 1, 0xd05);
        menu_text(m, TextKey::draw_party_overview_19, 1, 0xe05);
    }
    if (!floor_tokens)
        menu_text(m, 0, 1, 0xd0c, kTowerProgressText);
    if (!top_tokens)
        menu_text(m, 0, 1, 0xe05, kTowerCounterText);
    saved.restore(m);
    return;
}

// rom: 0x341A9F (near)
void draw_party_overview(Machine &m) {
    draw_party_overview_core(m);
    m.ret();
}
// Draw a member's name, class, HP and race-dependent secondary statistic.
void draw_member_summary_core(Machine &m) {
    if (m.rd8(m.ss(), m.si()) == 0xff) {

        return;
    }
    const auto saved_ax = m.ax(), saved_bx = m.bx(), saved_si = m.si(), saved_ds = m.ds();
    auto restore = [&]() {
        m.ax() = saved_ax;
        m.bx() = saved_bx;
        m.si() = saved_si;
        m.ds() = saved_ds;
    };
    ++m.si();
    m.cx() = 4;
    draw_text_n_white_core(m);
    restore();
    move_column(m, 4);
    m.cx() = 1;
    {
        const TextPtr text_ptr = text(m, TextKey::draw_member_summary_1);
        m.ds() = text_ptr.segment;
        m.si() = text_ptr.offset;
        draw_text_n_white_core(m);
        text_done(m, TextKey::draw_member_summary_1);
    }
    restore();
    move_column(m, 5);
    m.set_al(m.rd8(m.ss(), uint16_t(saved_si + 5)));
    lookup_name_record_core(m);
    m.ax() = saved_ax;
    draw_text_core(m);
    restore();
    move_row(m, 1);
    move_column(m, 10);
    m.cx() = 2;
    // a language with value tokens (text_tokens.h) gets the numbers in its strings instead
    const bool heart_tokens = has_value_tokens(m, TextKey::draw_member_summary_2);
    const bool hp_tokens = has_value_tokens(m, TextKey::draw_member_summary_3);
    {
        TextValues values;
        if (heart_tokens)
            values.set(U"hearts", member_hearts(m, m.ds(), saved_si));
        const TextPtr text_ptr = text(m, TextKey::draw_member_summary_2);
        m.ds() = text_ptr.segment;
        m.si() = text_ptr.offset;
        draw_text_n_white_core(m);
        text_done(m, TextKey::draw_member_summary_2);
    }
    restore();
    move_row(m, 1);
    move_column(m, 12);
    if (!heart_tokens) {
        m.si() = uint16_t(saved_si + 10);
        m.dx() = m.rd8(m.ds(), m.si()) & 15;
        decimal_to_bcd_clamped_core(m);
        m.ax() = saved_ax;
        draw_bcd_left_core(m);
    }
    restore();
    move_row(m, 1);
    m.cx() = 7;
    {
        TextValues values;
        if (hp_tokens) {
            values.set(U"hp", member_hp(m, m.ds(), saved_si));
            values.set(U"maxhp", member_maxhp(m, m.ds(), saved_si));
        }
        const TextPtr text_ptr = text(m, TextKey::draw_member_summary_3);
        m.ds() = text_ptr.segment;
        m.si() = text_ptr.offset;
        draw_text_n_white_core(m);
        text_done(m, TextKey::draw_member_summary_3);
    }
    restore();
    if (hp_tokens) { // BX as the condition's draw leaves it
        if (m.rd8(m.ds(), saved_si) != 0) {
            move_row(m, 1);
            move_column(m, 6);
        }
        return;
    }
    move_row(m, 1);
    move_column(m, 4);
    m.si() = uint16_t(saved_si + 6);
    m.dx() = m.rd16(m.ds(), m.si());
    decimal_to_bcd_clamped_core(m);
    m.ax() = saved_ax;
    m.cx() = 3;
    draw_bcd_number_core(m);
    restore();
    if (m.rd8(m.ds(), saved_si) == 0) {
        move_row(m, 1);
        move_column(m, 8);
        m.si() = uint16_t(saved_si + 8);
        m.dx() = m.rd16(m.ds(), m.si());
        decimal_to_bcd_clamped_core(m);
        m.ax() = saved_ax;
        m.cx() = 3;
        draw_bcd_number_core(m);
        restore();
    } else {
        move_row(m, 1);
        move_column(m, 6);
        const auto b2 = m.bx();
        uint8_t race = m.rd8(m.ss(), saved_si);
        unsigned bit = 0;
        while (bit < 8 && !(race & 1)) {
            race >>= 1;
            ++bit;
        }
        m.si() = m.rd16(m.cs(), uint16_t(0x872c + bit * 2));
        m.ds() = m.cs();
        m.cx() = 3;
        m.ax() = saved_ax;
        m.bx() = b2;
        draw_race_stat_core(m);
        restore();
        m.bx() = b2;
    }
}
// rom: 0x345004 (near)
// Draw a member's name, class, HP and race-dependent secondary statistic.
void draw_member_summary(Machine &m) {
    draw_member_summary_core(m);
    m.ret();
}

namespace {
// INC's flags are observable at load_menu's all-invalid return. Carry is retained.
void misc_inc_flags(Machine &m, uint16_t result, uint16_t carry) {
    const uint16_t previous_value = uint16_t(result - 1);
    uint16_t flags = uint16_t(m.flags() & ~(emu::CF | emu::OF | emu::AF | emu::SF | emu::ZF | emu::PF));
    flags |= carry;
    if (result == 0)
        flags |= emu::ZF;
    if (result & 0x8000)
        flags |= emu::SF;
    if (previous_value == 0x7fff)
        flags |= emu::OF;
    if ((previous_value ^ 1 ^ result) & 16)
        flags |= emu::AF;
    uint8_t byte = uint8_t(result);
    unsigned bits = 0;
    for (unsigned i = 0; i < 8; ++i)
        bits += (byte >> i) & 1;
    if (!(bits & 1))
        flags |= emu::PF;
    m.flags() = flags;
}
} // namespace
namespace {
void option_label_core(Machine &m) {
    if (!(m.ah() & m.al())) {
        const auto si = m.si();
        m.si() = m.di();
        m.di() = si;
    }
    m.ax() = 0;
    draw_text_core(m);
}
// Like option_label_core, but the SI/DI candidates are string keys: the chosen one is drawn, the other is
// left in DI as the original's swap leaves it.
void option_label_core(Machine &m, TextKey si_key, TextKey di_key) {
    const bool first = m.ah() & m.al();
    const TextKey key = first ? si_key : di_key;
    m.di() = text(m, first ? di_key : si_key).offset; // the original's xchg leaves the other one in DI
    const TextPtr text_ptr = text(m, key);
    m.ds() = text_ptr.segment;
    m.si() = text_ptr.offset;
    m.ax() = 0;
    draw_text_core(m);
    text_done(m, key);
}
void option_values_core(Machine &m) {
    m.ds() = m.cs();
    if (!(m.rd8(m.ss(), kOptions) & 1))
        menu_text(m, TextKey::draw_option_values_1, 0, 0x417);
    else {
        menu_text(m, TextKey::draw_option_values_2, 0, 0x414);
        m.set_al(m.rd8(m.ss(), kOptions));
        m.set_ah(4);
        m.bx() = 0x417;
        option_label_core(m, TextKey::draw_option_values_3, TextKey::draw_option_values_4);
    }
    constexpr TextKey siKeys[] = {TextKey::draw_option_values_5, TextKey::draw_option_values_7};
    constexpr TextKey diKeys[] = {TextKey::draw_option_values_6, TextKey::draw_option_values_8};
    for (unsigned i = 0; i < 2; ++i) {
        m.set_al(m.rd8(m.ss(), kOptions));
        m.set_ah(uint8_t(i ? 8 : 2));
        m.bx() = uint16_t(0x617 + i * 512);
        option_label_core(m, siKeys[i], diKeys[i]);
    }
    m.dx() = uint8_t(m.rd8(m.ss(), kDialogueSpeed) + 1);
    decimal_to_bcd_clamped_core(m);
    m.ax() = 0;
    m.bx() = 0xa19;
    draw_bcd_left_core(m);
}
// Set AL-indexed creation/event unlock bit while preserving registers.
void set_event_flag_core(Machine &m) {
    const auto id = m.al();
    if (id <= 0x95) {
        const auto destination = uint16_t(kEventFlags + (id >> 3));
        m.wr8(m.ss(), destination,
              uint8_t(m.rd8(m.ss(), destination) | m.rd8(m.cs(), uint16_t(0x5f21 + (id & 7)))));
    }
}
void creation_name_core(Machine &m) {
    m.ax() = uint16_t(m.rd8(m.ss(), kCreationRace) * 8);
    m.bx() = m.rd8(m.ss(), kMenuSelection);
    m.ax() = uint16_t(m.ax() + m.bx());
    m.si() = uint16_t(kCreationTypes + m.ax());
    m.ax() = m.rd8(m.cs(), m.si());
    lookup_name_record_core(m);
    m.ax() = 0;
    m.bx() = 0x10d;
    draw_text_core(m);
    m.ax() = 1;
    m.bx() = 0x15;
    m.dx() = 0;
    draw_creation_icon_core(m);
}
void creation_names_core(Machine &m) {
    m.ax() = uint16_t(m.rd8(m.ss(), kCreationRace) * 8);
    m.si() = uint16_t(kCreationTypes + m.ax());
    m.bp() = 0xaf94;
    m.cx() = 8;
    do {
        const auto cx = m.cx(), si = m.si(), bp = m.bp();
        m.ax() = m.rd8(m.cs(), m.si());
        lookup_name_record_core(m);
        m.ax() = m.rd16(m.cs(), m.bp());
        m.bx() = m.rd16(m.cs(), uint16_t(m.bp() + 2));
        draw_text_core(m);
        m.cx() = cx;
        m.si() = uint16_t(si + 1);
        m.bp() = uint16_t(bp + 4);
    } while (--m.cx());
}
// Build a 31-byte creation member from the monster template, including HP, race flags, stats and item uses.
void creation_template_core(Machine &m) {
    const auto ax = m.ax(), di = m.di();
    m.set_al(255);
    m.cx() = 31;
    m.ds() = m.es() = m.ss();
    m.flags() &= uint16_t(~emu::DF);
    for (unsigned i = 0; i < 31; ++i)
        m.wr8(m.es(), uint16_t(di + i), 255);
    m.di() = di;
    m.ax() = ax;
    m.cx() = 0x6000;
    m.ds() = m.cx();
    m.si() = table_at16(m, Table::monsters, m.ds(), uint16_t(kMonsterPointers + uint8_t(ax) * 2));
    m.wr8(m.ss(), uint16_t(di + 5), m.al());
    m.wr8(m.ss(), di, 0);
    for (unsigned i = 1; i <= 4; ++i)
        m.wr8(m.ss(), uint16_t(di + i), 255);
    m.ax() = table_at16(m, Table::monsters, m.ds(), uint16_t(m.si() + 3));
    m.wr16(m.ss(), uint16_t(di + 6), m.ax());
    m.wr16(m.ss(), uint16_t(di + 8), m.ax());
    // Template bit 80h selects monster (40h), bit 40h selects mutant (20h), otherwise human (80h); start with
    // 3 lives.
    const auto type = table_at(m, Table::monsters, m.ds(), m.si());
    m.wr8(m.ss(), uint16_t(di + 10), uint8_t(3 | ((type & 0x80) ? 0x40 : ((type & 0x40) ? 0x20 : 0x80))));
    for (unsigned i = 0; i < 4; ++i) {
        m.set_al(table_at(m, Table::monsters, m.ds(), uint16_t(m.si() + 5 + i)));
        m.wr8(m.ss(), uint16_t(di + 11 + i), m.al());
    }
    m.ax() = 0x9000;
    m.es() = m.ax();
    m.cx() = table_at(m, Table::monsters, m.ds(), uint16_t(m.si() + 2));
    if (m.cx()) {
        m.si() = uint16_t(m.si() + 11);
        m.di() = uint16_t(m.di() + 15);
        do {
            m.set_al(table_at(m, Table::monsters, m.ds(), m.si()));
            m.wr8(m.ss(), m.di(), m.al());
            m.dx() = uint16_t((m.dx() & 0xff00) | m.al());
            ++m.si();
            ++m.di();
            m.set_ah(0);
            m.ax() = uint16_t(m.ax() << 1);
            m.bx() = uint16_t(0x41e0 + m.ax());
            m.bx() = m.rd16(m.es(), m.bx());
            m.set_al(table_at(m, Table::item_records, m.es(), uint16_t(m.bx() + 3))); // uses
            // Special IDs FCh..FFh use FF as their count; other items take the item table's maximum uses.
            if (uint8_t(m.dx()) >= 252)
                m.set_al(255);
            m.wr8(m.ss(), m.di(), m.al());
            ++m.di();
        } while (--m.cx());
    }
}
void draw_inventory_core(Machine &m) {
    m.ax() = 0;
    m.ds() = 0;
    for (unsigned i = 0; i < 8; ++i) {
        m.ax() = 0;
        m.bx() = uint16_t(0x404 + i * 256);
        m.dx() = m.rd16(m.ds(), uint16_t(kInventory + i * 2));
        draw_item_entry_core(m);
    }
}
void draw_key_items_core(Machine &m) {
    const MenuRegisters saved(m);
    menu_text(m, TextKey::draw_key_items_1, 0, 0x312);
    m.ax() = 0;
    m.ds() = 0;
    for (unsigned i = 0; i < 9; ++i) {
        m.ax() = 0;
        m.bx() = uint16_t(0x513 + i * 256);
        m.dx() = m.rd16(m.ds(), uint16_t(kKeyItems + i * 2));
        draw_item_without_count_core(m);
    }
    saved.restore(m);
}
void draw_creation_races_core(Machine &m) {
    m.ax() = 1;
    m.bx() = 0x201;
    m.cx() = 0x101c;
    m.dx() = 0x1ff;
    fill_screen_rect_core(m);
    const auto ds = m.ds();
    m.ds() = 0x9000;
    m.si() = 0x3068;
    m.bx() = 0x91b;
    m.cx() = 0x202;
    m.ax() = 1;
    draw_tilemap_core(m);
    m.si() = 0x3078;
    m.bx() = 0x90b;
    m.cx() = 0x202;
    m.ax() = 1;
    draw_tilemap_core(m);
    m.ds() = ds;
    m.ax() = uint16_t(m.rd8(m.ss(), kCreationLeader) * 5);
    m.dx() = 0;
    m.bx() = 0x20c;
    m.cx() = 5;
    do {
        draw_creation_race_row_core(m);
        ++m.ax();
        ++m.dx();
        move_row(m, 3);
    } while (--m.cx());
}
void draw_review_members_core(Machine &m) {
    const MenuRegisters saved(m);
    load_party_menu_sprites_core(m);
    for (unsigned i = 0; i < 4; ++i)
        if (m.rd8(m.ss(), uint16_t(kGameState + i * 31)) != 255) {
            m.ax() = uint16_t(8 + i * 4);
            m.bx() = uint16_t(0x1010 + i * 0x1400);
            m.dx() = uint16_t(i * 0x204);
            set_sprite_16x16_core(m);
        }
    m.ax() = 0;
    m.bx() = 0x20f;
    m.cx() = 0xe0e;
    m.ax() = 0;
    m.ds() = 0;
    constexpr uint16_t positions[] = {0x205, 0x405, 0x705, 0x905};
    for (unsigned i = 0; i < 4; ++i) {
        m.ax() = uint16_t(i & 1);
        m.bx() = positions[i];
        m.si() = uint16_t(kGameState + i * 31);
        draw_member_summary_core(m);
    }
    saved.restore(m);
}
void frame(Machine &m, uint16_t page, uint16_t position, uint16_t size) {
    m.ax() = page;
    m.bx() = position;
    m.cx() = size;
    draw_window_core(m);
}
} // namespace
// rom: 0x3473F0 (near)
// Choose the enabled/disabled label by AH's mask in AL; SI/DI swap only when disabled.
void draw_option_label(Machine &m) {
    option_label_core(m);
    m.ret();
}
// Shared body of ROM 0x346B04
// Read the last-used SRAM slot, defaulting invalid bytes to slot zero. AH remains 10h.
void read_last_save_slot_core(Machine &m) {
    begin_save_list(m); // (Save Slots option: a list is opening)
    m.ax() = uint16_t(0x1000 | m.rd8(0x1000, 0x3300));
    m.si() = 0x3300;
    if (m.al() > 2)
        m.set_al(0);
    // The save store / Save Slots 9: the last-used slot may be one the store holds (with a store attached,
    // SRAM's byte isn't used: no last slot means slot 1); with 9, scroll it into view and return its row
    if (SaveSlots *slots = save_store(m)) {
        const int slot = slots->last() >= 0 ? slots->last() : slots->store_attached() ? 0 : m.al();
        m.set_al(slots->extended() ? slots->show(slot) : uint8_t(slot < SaveSlots::kOriginal ? slot : 0));
    }
    return;
}

// rom: 0x346B04 (near)
void read_last_save_slot(Machine &m) {
    read_last_save_slot_core(m);
    m.ret();
}
// rom: 0x340227
// Clear all 256 hardware palette words; preserve registers and clear DF.
void clear_hardware_palettes(Machine &m) {
    m.flags() &= uint16_t(~emu::DF);
    for (unsigned i = 0; i < 256; ++i)
        m.wr16(0xfe0, uint16_t(i * 2), 0);
    m.retf();
}
// rom: 0x340200
// Enable the colour hardware mode when available; preserve the caller's AX/BX.
void set_color_hardware_mode(Machine &m) {
    const auto ax = m.ax();
    m.set_al(m.in8(0xa0) & 2);
    if (m.al()) {
        m.set_al(7);
        m.out8(0xa0, m.al());
        m.set_al(uint8_t((m.in8(0x60) & 5) | uint8_t(ax)));
        m.out8(0x60, m.al());
    }
    m.ax() = ax;
    m.retf();
}
// rom: 0x345EF9 (near)
// Set an in-range story event bit; preserve all registers.
void set_event_flag(Machine &m) {
    set_event_flag_core(m);
    m.ret();
}
// Shared body of ROM 0x345EB7
// Log AX's low/high bytes in a 100-entry circular progress history and set AL's event bit.
void append_progress_history_core(Machine &m) {
    const auto ax = m.ax();
    set_event_flag_core(m);
    uint16_t history = m.rd16(m.cs(), uint16_t(kProgressPointers + uint8_t(m.bx()) * 2));
    auto count = m.rd16(m.ss(), history);
    if (count != 999)
        m.wr16(m.ss(), history, uint16_t(count + 1));
    auto index = m.rd16(m.ss(), uint16_t(history + 2));
    m.wr8(m.ss(), uint16_t(history + 4 + index), uint8_t(ax));
    m.wr8(m.ss(), uint16_t(history + 104 + index), uint8_t(ax >> 8));
    m.wr16(m.ss(), uint16_t(history + 2), index ? uint16_t(index - 1) : 99);
    return;
}

// rom: 0x345EB7
void append_progress_history(Machine &m) {
    append_progress_history_core(m);
    m.retf();
}
// Shared body of ROM 0x3491E1
// Restore the ordinary field display sprite range and both scroll registers.
void restore_field_menu_display_core(Machine &m) {
    m.set_al(m.rd8(m.ss(), kMenuSavedSpriteCount));
    m.set_al(0);
    m.out8(6, 0);
    m.ax() = 8;
    m.out8(0x10, 8);
    m.out8(0x11, 0);
    m.out8(0x12, 8);
    m.out8(0x13, 0);
    return;
}

// rom: 0x3491E1 (near)
void restore_field_menu_display(Machine &m) {
    restore_field_menu_display_core(m);
    m.ret();
}
// Shared body of ROM 0x3491A8
// Clear the overlay, set menu scrolling, save sprite count and reset key repeat timer.
void prepare_field_menu_display_core(Machine &m) {
    m.ax() = 8;
    m.out8(0x10, 8);
    m.out8(0x11, 0);
    m.ax() = 0xfc08;
    m.out8(0x12, 8);
    m.out8(0x13, 0xfc);
    m.ax() = 1;
    m.bx() = 0;
    m.cx() = 0x2020;
    m.dx() = 0x1ff;
    fill_screen_rect_core(m);
    m.ax() = 40;
    m.wr16(m.ss(), kKeyRepeatTimer, m.ax());
    m.set_al(m.in8(6));
    m.wr8(m.ss(), kMenuSavedSpriteCount, m.al());
    m.set_al(4);
    m.out8(6, 4);
    return;
}

// rom: 0x3491A8 (near)
void prepare_field_menu_display(Machine &m) {
    prepare_field_menu_display_core(m);
    m.ret();
}
// rom: 0x3484BF (near)
// Classify FF/special item entries and FE infinite-use counts for menu drawing.
void classify_item_entry_call_core(Machine &m) {
    const auto item = m.rd8(0, m.bx()), count = m.rd8(0, uint16_t(m.bx() + 1));
    m.ax() = uint16_t((count == 255 || item >= 252 ? 1 : 0) | ((count == 254 ? 1 : 0) << 8));
}
void classify_item_entry(Machine &m) {
    classify_item_entry_call_core(m);
    m.ret();
}
// Shared body of ROM 0x346A0C
// Save AL's slot twice with checksum/magic bytes; preserve registers and return CF clear.
void write_save_slot_core(Machine &m) {
    if (SaveSlots *slots = save_store(m)) { // the save store / Save Slots 9 (save_slots.h)
        slots->set_last(m.al());
        if (slots->holds(m.al())) {
            // a slot the store holds: the same payload (magic bytes set first, as below) into the store; no
            // SRAM byte is written
            m.wr8(m.ss(), kSaveMagic1, 0x1b);
            m.wr8(m.ss(), kSaveMagic2, 0xe4);
            std::vector<uint8_t> payload(SaveSlots::kPayload);
            for (size_t i = 0; i < payload.size(); ++i)
                payload[i] = m.rd8(m.ss(), uint16_t(kGameState + i));
            slots->store(m.al(), payload, Session::of(m.wsc()).player_names().values);
            m.flags() &= uint16_t(~emu::CF);
            return;
        }
    }
    const MenuRegisters saved(m);
    m.ax() = m.al();
    m.es() = 0x1000;
    m.ds() = m.ss();
    m.di() = 0x3300;
    m.wr8(m.es(), m.di(), m.al());
    m.dx() = 0x43b;
    const uint32_t product = uint32_t(m.ax()) * m.dx();
    m.ax() = uint16_t(product);
    m.dx() = uint16_t(product >> 16);
    m.di() = uint16_t(0x3301 + m.ax());
    m.cx() = 0x439;
    m.wr8(m.ss(), kSaveMagic1, 0x1b);
    m.wr8(m.ss(), kSaveMagic2, 0xe4);
    copy_save_payload_core(m);
    m.di() = uint16_t(m.di() + 0xcb1);
    copy_save_payload_core(m);
    saved.restore(m);
    m.flags() &= uint16_t(~emu::CF);
    return;
}

// rom: 0x346A0C (near)
void write_save_slot(Machine &m) {
    write_save_slot_core(m);
    m.ret();
}
// Shared body of ROM 0x346ACE
// Test whether any of the three SRAM slots is valid. CF set means a valid slot exists.
void load_menu_core(Machine &m) {
    begin_save_list(m); // (Save Slots option)
    const MenuRegisters saved(m);
    m.ax() = 0;
    m.cx() = 3;
    if (const SaveSlots *slots = extended_save_slots(m)) // Save Slots 9: any of the nine counts
        m.cx() = uint16_t(slots->count());
    bool valid = false;
    if (const SaveSlots *slots = save_store(m); slots && slots->auto_listed()) { // ... and the autosave
        std::vector<uint8_t> block;
        valid = slots->load(SaveSlots::kAuto, &block);
    }
    do {
        m.di() = kSaveScratch;
        load_save_slot_core(m);
        if (!(m.flags() & emu::CF)) {
            valid = true;
            break;
        }
        const auto carry = m.flags() & emu::CF;
        ++m.ax();
        misc_inc_flags(m, m.ax(), carry);
    } while (--m.cx());
    if (!valid && !(save_store(m) && save_store(m)->store_attached())) { // (a store: SRAM isn't used)
        m.es() = 0x1000;
        m.ax() = 0x1000;
        m.set_al(0);
        m.si() = 0x3300;
        m.wr8(m.es(), m.si(), 0);
    }
    saved.restore(m);
    if (valid)
        m.flags() |= emu::CF;
    else
        m.flags() &= uint16_t(~emu::CF);
    return;
}

// rom: 0x346ACE
void load_menu(Machine &m) {
    load_menu_core(m);
    m.retf();
}
// rom: 0x34B779 (near)
// Draw the selected creation type's name and selection icon.
void draw_creation_name(Machine &m) {
    creation_name_core(m);
    m.ret();
}
// rom: 0x34AF5C (near)
// Draw eight type names using the CS-relative position table.
void draw_creation_type_names(Machine &m) {
    creation_names_core(m);
    m.ret();
}
// rom: 0x34B21A (near)
// Initialize the temporary human template, stats and item/count pairs. DF cleared, DS/ES changed.
void initialize_creation_template(Machine &m) {
    creation_template_core(m);
    m.ret();
}
// rom: 0x34737C (near)
// Draw running/battle/sound settings and the dialogue speed value.
void draw_option_values(Machine &m) {
    option_values_core(m);
    m.ret();
}
// Shared body of ROM 0x34B6A2
// Draw the selected name-entry alphabet as nine rows of ten glyphs with a middle spacing column.
void draw_name_entry_alphabet_core(Machine &m) {
    m.ds() = m.cs();
    const auto page = m.rd8(m.ss(), kNamePage);
    m.set_al(page);
    const TextKey alphabet_key = page == 2   ? TextKey::name_entry_alphabet_3
                                 : page == 1 ? TextKey::draw_name_entry_alphabet_2
                                             : TextKey::draw_name_entry_alphabet_1;
    const TextPtr label = text(m, alphabet_key);
    m.ds() = label.segment;
    m.si() = label.offset;
    m.ax() = 1;
    m.bx() = 0x1002;
    draw_text_core(m);
    text_done(m, alphabet_key);
    m.ax() = uint16_t(m.rd8(m.ss(), kNamePage) * 2);
    const TextKey sheet = name_entry_sheet(m.rd8(m.ss(), kNamePage)); // D_34B7B2[page]: 9 x 10 characters
    const TextPtr grid =
        page < 3 ? text(m, sheet) : TextPtr{m.cs(), m.rd16(m.cs(), uint16_t(0xB7B2 + m.ax()))};
    m.ds() = grid.segment;
    m.si() = grid.offset;
    m.bp() = 0xb70c;
    m.cx() = 9;
    do {
        const auto outer_bx = m.bx(), outer_cx = m.cx();
        m.bx() = m.rd16(m.cs(), uint16_t(m.bp() + 2));
        m.cx() = 10;
        do {
            const auto bx = m.bx(), cx = m.cx(), si = m.si();
            m.ax() = m.rd16(m.cs(), m.bp());
            m.cx() = 1;
            draw_text_n_white_core(m);
            m.bx() = bx;
            m.cx() = cx;
            m.si() = si;
            move_column(m, 2);
            if (m.cx() == 6)
                move_column(m, 1);
            ++m.si();
        } while (--m.cx());
        m.cx() = outer_cx;
        m.bx() = outer_bx;
        m.bp() = uint16_t(m.bp() + 4);
    } while (--m.cx());
    text_done(m, sheet);
    return;
}

// rom: 0x34B6A2 (near)
void draw_name_entry_alphabet(Machine &m) {
    draw_name_entry_alphabet_core(m);
    m.ret();
}
// Shared body of ROM 0x34B59F
// Draw name-entry frames, labels, selected type and corner icons; registers preserved.
void draw_name_entry_screen_core(Machine &m) {
    const MenuRegisters saved(m);
    constexpr uint16_t windows[][2] = {
        {1, 0x30b}, {12, 0x311}, {0x301, 0xf1c}, {0x301, 0xd1c}, {0x307, 0xf16}};
    for (auto &w : windows)
        frame(m, 0, w[0], w[1]);
    constexpr uint16_t positions[] = {0x102, 0x502, 0x603, 0x702, 0x802, 0x902, 0xa03, 0xc02, 0xd04};
    constexpr TextKey labels[] = {TextKey::draw_name_entry_screen_1, TextKey::draw_name_entry_screen_2,
                                  TextKey::draw_name_entry_screen_3, TextKey::draw_name_entry_screen_4,
                                  TextKey::draw_name_entry_screen_5, TextKey::draw_name_entry_screen_6,
                                  TextKey::draw_name_entry_screen_7, TextKey::draw_name_entry_screen_8,
                                  TextKey::draw_name_entry_screen_9};
    for (unsigned i = 0; i < 9; ++i)
        menu_text(m, labels[i], 0, positions[i]);
    creation_name_core(m);
    m.ax() = 0x9000;
    m.ds() = m.ax();
    m.si() = 0x3078;
    m.bx() = 0xf07;
    m.cx() = 0x202;
    m.ax() = 1;
    draw_tilemap_core(m);
    m.si() = 0x3068;
    m.bx() = 0xf1b;
    m.cx() = 0x202;
    m.ax() = 1;
    draw_tilemap_core(m);
    saved.restore(m);
    return;
}

// rom: 0x34B59F (near)
void draw_name_entry_screen(Machine &m) {
    draw_name_entry_screen_core(m);
    m.ret();
}
// Shared body of ROM 0x34AEE2
// Draw the creation type-list frames, title and eight type names, preserving registers.
void draw_creation_type_screen_core(Machine &m) {
    const MenuRegisters saved(m);
    frame(m, 0, 1, 0x30c);
    frame(m, 0, 13, 0x310);
    frame(m, 0, 0x301, 0xd1c);
    frame(m, 0, 0x30c, 0xd11);
    frame(m, 0, 0x1001, 0x21c);
    menu_text(m, TextKey::draw_creation_type_screen_1, 0, 0x102);
    menu_text(m, TextKey::draw_creation_type_screen_2, 1, 0x1003);
    creation_names_core(m);
    saved.restore(m);
    return;
}

// rom: 0x34AEE2 (near)
void draw_creation_type_screen(Machine &m) {
    draw_creation_type_screen_core(m);
    m.ret();
}
// Shared body of ROM 0x34640A
// Draw save confirmation text and record whether the selected slot is occupied.
void draw_save_confirmation_core(Machine &m) {
    const MenuRegisters saved(m);
    m.ax() = 0;
    m.ds() = 0;
    frame(m, 0, 1, 0x31c);
    m.ax() = 1;
    m.bx() = 1;
    m.cx() = 0x21c;
    m.dx() = 0x1ff;
    fill_screen_rect_core(m);
    menu_text(m, TextKey::draw_save_confirmation_1, 1, 0x113);
    menu_text(m, TextKey::draw_save_confirmation_2, 1, 0x117);
    m.ax() = save_slot_for_row(m, m.rd8(m.ss(), kSaveSlotSelection));
    m.di() = kSaveScratch;
    load_save_slot_core(m);
    if (m.flags() & emu::CF) {
        m.wr8(m.ss(), kSaveChoice, 0);
        menu_text(m, TextKey::draw_save_confirmation_3, 1, 2);
    } else {
        m.wr8(m.ss(), kSaveChoice, 1);
        menu_text(m, TextKey::draw_save_confirmation_4, 1, 0x102);
        menu_text(m, TextKey::draw_save_confirmation_5, 1, 2);
    }
    saved.restore(m);
    return;
}

// rom: 0x34640A (near)
void draw_save_confirmation(Machine &m) {
    draw_save_confirmation_core(m);
    m.ret();
}
// Shared body of ROM 0x346311
// Redraw save panels after saving. Original omits several unused window/text calls.
void redraw_save_panels_core(Machine &m) {
    const MenuRegisters saved(m);
    m.ax() = 0;
    m.ds() = 0;
    for (unsigned i = 0; i < 8; ++i) {
        m.ax() = uint16_t(i * 4);
        m.bx() = 0xe0;
        m.dx() = 0x3ffb;
        set_sprite_16x16_core(m);
    }
    frame(m, 0, 1, 0x121c);
    m.ax() = 1;
    m.bx() = 1;
    m.cx() = 0x121c;
    m.dx() = 0x1ff;
    fill_screen_rect_core(m);
    frame(m, 0, 1, 0xd1c);
    frame(m, 0, 1, 0x81c);
    m.ds() = m.cs();
    m.ax() = 1;
    m.bx() = 0x110;
    m.si() = 0xbc4c;
    draw_save_slot_summaries_core(m);
    saved.restore(m);
    return;
}

// rom: 0x346311 (near)
void redraw_save_panels(Machine &m) {
    redraw_save_panels_core(m);
    m.ret();
}
// rom: 0x34728D (near)
// Draw options-menu frames, labels and values, preserving registers.
void draw_options_screen_core(Machine &m) {
    const MenuRegisters saved(m);
    m.ax() = 0;
    m.ds() = 0;
    for (unsigned i = 0; i < 3; ++i) {
        m.ax() = uint16_t(i * 4);
        m.bx() = 0xe0;
        m.dx() = 0x3ffb;
        set_sprite_16x16_core(m);
    }
    frame(m, 0, 1, 0x121c);
    frame(m, 0, 0x201, 0xe1c);
    frame(m, 0, 1, 0x307);
    frame(m, 0, 0x1001, 0x21c);
    constexpr uint16_t positions[] = {0x102, 0x10e, 0x404, 0x604, 0x804, 0xa04, 0xc04, 0xe04};
    constexpr TextKey labels[] = {TextKey::draw_options_screen_1, TextKey::draw_options_screen_2,
                                  TextKey::draw_options_screen_3, TextKey::draw_options_screen_4,
                                  TextKey::draw_options_screen_5, TextKey::draw_options_screen_6,
                                  TextKey::draw_options_screen_7, TextKey::draw_options_screen_8};
    for (unsigned i = 0; i < 8; ++i)
        menu_text(m, labels[i], 0, positions[i]);
    option_values_core(m);
    saved.restore(m);
    return;
}
void draw_options_screen(Machine &m) {
    draw_options_screen_core(m);
    m.ret();
}
// rom: 0x345CA5 (near)
// Clear the race-selection area and draw five race rows plus corner icons.
void draw_creation_races_entry_core(Machine &m) {
    draw_creation_races_core(m);
    return;
}
void draw_creation_races(Machine &m) {
    draw_creation_races_entry_core(m);
    m.ret();
}
// rom: 0x345577 (near)
// Draw race-selection screen frames, category and instruction labels; registers preserved.
void draw_race_selection_screen_core(Machine &m) {
    const MenuRegisters saved(m);
    m.ax() = 0;
    m.ds() = 0;
    for (unsigned i = 0; i < 4; ++i) {
        m.ax() = uint16_t(i * 4);
        m.bx() = 0xe0;
        m.dx() = 0x3ffb;
        set_sprite_16x16_core(m);
    }
    frame(m, 0, 1, 0x121c);
    m.ax() = 1;
    m.bx() = 1;
    m.cx() = 0x121c;
    m.dx() = 0x1ff;
    fill_screen_rect_core(m);
    frame(m, 0, 1, 0x120a);
    frame(m, 0, 11, 0x1212);
    frame(m, 0, 11, 0x212);
    frame(m, 0, 1, 0x30a);
    menu_text(m, TextKey::draw_race_selection_screen_1, 0, 0x102);
    menu_text(m, TextKey::draw_race_selection_screen_2, 1, 14);
    draw_creation_races_core(m);
    menu_text(m, TextKey::draw_race_selection_screen_3, 1, 0xf03);
    menu_text(m, TextKey::draw_race_selection_screen_4, 1, 0x1003);
    saved.restore(m);
    return;
}
void draw_race_selection_screen(Machine &m) {
    draw_race_selection_screen_core(m);
    m.ret();
}
// rom: 0x344F60 (near)
// Reload party graphics and draw four member summaries on the review screen.
void draw_review_members(Machine &m) {
    draw_review_members_core(m);
    m.ret();
}
// rom: 0x344F46 (near)
// Draw review cursor sprite and member panels, preserving registers.
void draw_review_screen_core(Machine &m) {
    const MenuRegisters saved(m);
    m.ax() = 24;
    m.bx() = 0x3092;
    m.dx() = 0x3ffb;
    set_sprite_16x16_core(m);
    draw_review_members_core(m);
    saved.restore(m);
    return;
}
void draw_review_screen(Machine &m) {
    draw_review_screen_core(m);
    m.ret();
}
// Shared body of ROM 0x342BFC
// Draw eight inventory item rows from the live party state.
void draw_inventory_items_core(Machine &m) {
    draw_inventory_core(m);
    return;
}

// rom: 0x342BFC (near)
void draw_inventory_items(Machine &m) {
    draw_inventory_items_core(m);
    m.ret();
}
// rom: 0x342C8A (near)
// Draw nine key-item names in the right-hand list; registers preserved.
void draw_key_items(Machine &m) {
    draw_key_items_core(m);
    m.ret();
}
// rom: 0x342B08 (near)
// Draw inventory windows, item lists and scroll markers; registers preserved.
void draw_inventory_screen_core(Machine &m) {
    const MenuRegisters saved(m);
    m.ax() = 0;
    m.ds() = 0;
    for (unsigned i = 0; i < 4; ++i) {
        m.ax() = uint16_t(8 + i * 4);
        m.bx() = 0x9600;
        m.dx() = uint16_t(i * 0x204);
        set_sprite_16x16_core(m);
    }
    frame(m, 0, 1, 0xf1c);
    frame(m, 0, 0x201, 0xe10);
    frame(m, 0, 0x211, 0xe0c);
    frame(m, 0, 1, 0x308);
    frame(m, 0, 0x1001, 0x21c);
    menu_text(m, TextKey::draw_inventory_screen_1, 0, 0x103);
    menu_text(m, TextKey::draw_inventory_screen_2, 1, 9);
    menu_text(m, TextKey::draw_inventory_screen_3, 1, 0xc05);
    draw_inventory_core(m);
    m.ax() = 0x9000;
    m.ds() = m.ax();
    m.si() = 0x3040;
    m.bx() = 0xd04;
    m.cx() = 0x202;
    m.ax() = 1;
    draw_tilemap_core(m);
    const auto ds = m.ds();
    const TextPtr icon = text(m, TextKey::inventory_screen_icon); // D_342BFB: one font tile
    m.ds() = icon.segment;
    m.bx() = 0xc04;
    m.si() = icon.offset;
    m.cx() = 1;
    m.ax() = 1;
    draw_text_n_white_core(m);
    m.ds() = ds;
    draw_key_items_core(m);
    saved.restore(m);
    return;
}
void draw_inventory_screen(Machine &m) {
    draw_inventory_screen_core(m);
    m.ret();
}
// Shared body of ROM 0x34AFB4
// Show the selected creation type's walking graphic, template stats and equipment. Several ROM draw calls
// are deliberately absent: only HP and the four primary stats are actually rendered.
void draw_creation_type_preview_core(Machine &m) {
    m.ax() = 0;
    m.ds() = 0;
    frame(m, 0, 0x30c, 0xd11);
    const auto ds = m.ds();
    m.ax() = uint16_t(m.rd8(m.ss(), kCreationRace) * 8);
    m.set_al(uint8_t(m.al() + m.rd8(m.ss(), kMenuSelection)));
    m.si() = uint16_t(kCreationTypes + m.ax());
    m.set_al(m.rd8(m.cs(), m.si()));
    char_type_to_gfx_index_core(m);
    const auto gfx = m.ax();
    m.ax() = uint8_t(gfx);
    m.ax() = uint16_t(m.ax() * 4);
    m.bp() = m.ax();
    m.es() = 0x9000;
    m.ds() = m.rd16(m.es(), uint16_t(m.bp() + 0x3f70));
    m.ax() = 0xd4;
    m.cx() = 4;
    m.si() = m.rd16(m.es(), uint16_t(m.bp() + 0x40b0));
    copy_tiles_core(m);
    m.ax() = uint16_t((gfx >> 8) << 5);
    m.si() = uint16_t(m.rd16(m.es(), uint16_t(m.bp() + 0x40b2)) + m.ax());
    m.di() = kPaletteBuffer;
    m.ax() = 8;
    m.cx() = 1;
    copy_palettes_core(m);
    m.ds() = ds;
    m.ax() = 1;
    m.bx() = 0x40d;
    m.dx() = 0;
    draw_creation_icon_core(m);
    m.ax() = uint16_t(m.rd8(m.ss(), kCreationRace) * 8);
    m.set_al(uint8_t(m.al() + m.rd8(m.ss(), kMenuSelection)));
    m.si() = uint16_t(kCreationTypes + m.ax());
    m.set_al(m.rd8(m.cs(), m.si()));
    m.di() = kSaveScratch;
    creation_template_core(m);
    m.si() = kSaveScratch;
    m.ax() = 0;
    m.ds() = 0;
    m.ax() = 0;
    m.bx() = 0x408;
    m.cx() = 4;
    m.set_al(m.rd8(m.ss(), uint16_t(kSaveScratch + 5)));
    lookup_name_record_core(m);
    m.ax() = 0;
    m.bx() = 0x40d;
    auto label_key = [&](TextKey key, uint16_t position, uint16_t count) {
        const auto ds = m.ds();
        const TextPtr text_ptr = text(m, key);
        m.ds() = text_ptr.segment;
        m.si() = text_ptr.offset;
        m.ax() = 0;
        m.bx() = position;
        m.cx() = count;
        draw_text_n_white_core(m);
        text_done(m, key);
        m.ds() = ds;
        m.si() = kSaveScratch;
    };
    // a language with value tokens (text_tokens.h) gets the numbers in its strings instead
    {
        TextValues values;
        values.set(U"hp", decimal_text(m.rd16(m.ss(), uint16_t(kSaveScratch + 6)), 3));
        label_key(TextKey::draw_creation_type_preview_1, 0x50f, 2);
    }
    m.si() = uint16_t(kSaveScratch + 6);
    m.dx() = m.rd16(m.ss(), m.si());
    decimal_to_bcd_clamped_core(m);
    m.ax() = 0;
    m.bx() = 0x513;
    m.cx() = 3;
    if (!has_value_tokens(m, TextKey::draw_creation_type_preview_1))
        draw_bcd_number_core(m);
    m.si() = uint16_t(kSaveScratch + 8);
    m.dx() = m.rd16(m.ss(), m.si());
    decimal_to_bcd_clamped_core(m);
    m.ax() = 0;
    m.bx() = 0x510;
    m.cx() = 3;
    m.si() = 0xb9a3;
    m.ax() = 0;
    m.bx() = 0x512;
    m.cx() = 2;
    m.si() = uint16_t(kSaveScratch + 10);
    m.dx() = m.rd8(m.ss(), m.si()) & 15;
    decimal_to_bcd_clamped_core(m);
    m.ax() = 0;
    m.bx() = 0x514;
    constexpr TextKey labels[] = {
        TextKey::draw_creation_type_preview_2, TextKey::draw_creation_type_preview_4,
        TextKey::draw_creation_type_preview_6, TextKey::draw_creation_type_preview_8};
    constexpr TextKey separators[] = {
        TextKey::draw_creation_type_preview_3, TextKey::draw_creation_type_preview_5,
        TextKey::draw_creation_type_preview_7, TextKey::draw_creation_type_preview_9};
    for (unsigned i = 0; i < 4; ++i) {
        label_key(labels[i], uint16_t(0x70d + i * 512), 4);
        {
            TextValues values;
            values.set(
                U"value",
                decimal_text(std::min<unsigned>(m.rd8(m.ss(), uint16_t(kSaveScratch + 11 + i)), 99), 2));
            label_key(separators[i], uint16_t(0x80f + i * 512), 1);
        }
        m.si() = uint16_t(kSaveScratch + 11 + i);
        m.dx() = m.rd8(m.ss(), m.si());
        if (m.dx() >= 99)
            m.dx() = 99;
        decimal_to_bcd_clamped_core(m);
        m.ax() = 0;
        m.bx() = uint16_t(0x811 + i * 512);
        m.cx() = 2;
        if (!has_value_tokens(m, separators[i]))
            draw_bcd_number_core(m);
    }
    m.si() = kSaveScratch;
    m.ax() = 0;
    m.ds() = 0;
    m.ax() = 0;
    m.cx() = 8;
    m.bx() = 0x714;
    m.si() = uint16_t(m.si() + 15);
    draw_item_entry_rows_core(m);
    return;
}

// rom: 0x34AFB4 (near)
void draw_creation_type_preview(Machine &m) {
    draw_creation_type_preview_core(m);
    m.ret();
}

namespace {
void misc_out16(Machine &m, uint8_t port, uint16_t value) {
    m.out8(port, uint8_t(value));
    m.out8(uint8_t(port + 1), uint8_t(value >> 8));
}
void restore_menu_dma(Machine &m, uint16_t source, uint16_t destination, uint16_t size) {
    misc_out16(m, 0x42, 0);
    misc_out16(m, 0x40, source);
    misc_out16(m, 0x44, destination);
    misc_out16(m, 0x46, size);
    m.out8(0x48, 0x80);
}
void direction_choice_icon_core(Machine &m) {
    const auto ax = m.ax(), bx = m.bx(), cx = m.cx(), dx = m.dx(), ds = m.ds();
    m.ds() = 0x9000;
    m.si() = uint16_t(0x3f46 + uint16_t(m.dx() << 3));
    m.cx() = 0x202;
    draw_tilemap_core(m);
    m.ax() = ax;
    m.bx() = bx;
    m.cx() = cx;
    m.dx() = dx;
    m.ds() = ds;
}
} // namespace
// rom: 0x340240
// Copy CX palettes from DS:SI straight to FE00h + AX*32; ES/DI/CX preserved, DF cleared.
void copy_direct_palettes(Machine &m) {
    m.flags() &= uint16_t(~emu::DF);
    uint16_t dest = uint16_t(m.ax() << 5), n = uint16_t(m.cx() << 4);
    m.ax() = 0xfe0;
    for (unsigned i = 0; i < n; ++i) {
        m.wr16(0xfe0, dest, m.rd16(m.ds(), m.si()));
        dest = uint16_t(dest + 2);
        m.si() = uint16_t(m.si() + 2);
    }
    m.retf();
}
// rom: 0x340279
// Copy CX monochrome tiles (16 bytes) from DS:SI to 2000h + AX*16; ES/DI preserved, CX zero.
void copy_mono_tiles(Machine &m) {
    m.flags() &= uint16_t(~emu::DF);
    uint16_t dest = uint16_t(m.ax() << 4);
    m.cx() = uint16_t(m.cx() << 3);
    m.ax() = 0x200;
    while (m.cx()) {
        m.wr16(0x200, dest, m.rd16(m.ds(), m.si()));
        dest = uint16_t(dest + 2);
        m.si() = uint16_t(m.si() + 2);
        --m.cx();
    }
    m.retf();
}
// Shared body of ROM 0x340BC9.
// Look up a signed waveform sample using AL, preserving BX.
void lookup_wave_sample_core(Machine &m) { m.ax() = m.rd16(m.cs(), uint16_t(0xcd7 + m.al() * 2)); }
// rom: 0x340BC9
// See lookup_wave_sample_core.
void lookup_wave_sample(Machine &m) {
    lookup_wave_sample_core(m);
    m.retf();
}
// Shared body of ROM 0x3411A0
// Resolve the ordered monster-family pair at the higher rank; FF means no transformation.
void lookup_meat_transformation_core(Machine &m) {
    auto member_type = m.al(), meat_type = m.ah();
    // Ordered family pair = member_family*29 + meat_family; result row uses max(member_rank, meat_rank).
    auto family_pair = table_at(m, Table::monster_type_family, 0x9000, uint16_t(0x7ad0 + meat_type)) +
                       table_at(m, Table::monster_type_family, 0x9000, uint16_t(0x7ad0 + member_type)) * 29;
    m.ax() = table_at(m, Table::meat_pairs, 0x9000, uint16_t(0x7b98 + family_pair));
    if (m.al() != 255) {
        auto member_rank = table_at(m, Table::monster_type_rank, 0x9000, uint16_t(0x7a08 + member_type)),
             meat_rank = table_at(m, Table::monster_type_rank, 0x9000, uint16_t(0x7a08 + meat_type));
        m.ax() =
            table_at(m, Table::meat_results, 0x9000,
                     uint16_t(0x7e6d + m.ax() * 16 + (member_rank >= meat_rank ? member_rank : meat_rank)));
    }
    return;
}

// rom: 0x3411A0
// Resolve the meat table for AL/AH monster types. FF means no transformation; other registers preserved.
void lookup_meat_transformation(Machine &m) {
    lookup_meat_transformation_core(m);
    m.retf();
}
// Shared body of ROM 0x3411F8
// Replace member AL&3 with monster AH, restoring template HP/stats and up to eight ability slots.
void initialize_monster_member_core(Machine &m) {
    uint16_t member = uint16_t(kGameState + (m.al() & 3) * 31);
    auto type = m.ah();
    uint16_t source = table_at16(m, Table::monsters, 0x6000, uint16_t(kMonsterPointers + type * 2));
    m.wr8(m.ss(), member, 0);
    m.wr8(m.ss(), uint16_t(member + 5), type);
    auto hp = table_at16(m, Table::monsters, 0x6000, uint16_t(source + 3));
    m.wr16(m.ss(), uint16_t(member + 6), hp);
    m.wr16(m.ss(), uint16_t(member + 8), hp);
    for (unsigned i = 0; i < 4; ++i)
        m.wr8(m.ss(), uint16_t(member + 11 + i),
              table_at(m, Table::monsters, 0x6000, uint16_t(source + 5 + i)));
    for (unsigned i = 0; i < 16; ++i)
        m.wr8(m.ss(), uint16_t(member + 15 + i), 255);
    uint8_t ability_count = table_at(m, Table::monsters, 0x6000, uint16_t(source + 2));
    if (ability_count >= 8)
        ability_count = 8;
    // Original-game bug kept: a zero template count enters this loop and wraps to 256 copies, overrunning the
    // member.
    do {
        auto item = table_at(m, Table::monsters, 0x6000, uint16_t(source + 11));
        m.wr8(m.ss(), uint16_t(member + 15), item);
        m.wr8(m.ss(), uint16_t(member + 16),
              table_at(m, Table::item_records, 0x9000, uint16_t(0x43e3 + item * 8)));
        ++source;
        member = uint16_t(member + 2);
    } while (--ability_count);
    return;
}

// rom: 0x3411F8
// Initialize member AL&3 from monster AH's template. A zero equipment count still copies 256 entries.
void initialize_monster_member(Machine &m) {
    initialize_monster_member_core(m);
    m.retf();
}
// rom: 0x3412B2
// Return the AL-indexed character descriptor, preserving DS/BX.
void lookup_character_descriptor(Machine &m) {
    m.ax() = table_at16(m, Table::monster_type_descriptor, 0x9000, uint16_t(0x7878 + m.al() * 2));
    m.retf();
}
// rom: 0x341E5F (near)
// Restore the saved menu buffer by DMA; registers and flags preserved.
void restore_menu_sprites(Machine &m) {
    restore_menu_dma(m, kSpriteBackup, kSpriteBuffer, 0x200);
    m.ret();
}
// rom: 0x341E7A (near)
// Restore the saved menu buffer by DMA; registers and flags preserved.
void restore_menu_palettes(Machine &m) {
    restore_menu_dma(m, kPaletteTarget, kPaletteBuffer, 0x200);
    m.ret();
}
// rom: 0x341E95 (near)
// Restore the saved menu buffer by DMA; registers and flags preserved.
void restore_menu_screen_one_call_core(Machine &m) { restore_menu_dma(m, kOverlayScreen, kScreenMap, 0x800); }
void restore_menu_screen_one(Machine &m) {
    restore_menu_screen_one_call_core(m);
    m.ret();
}
// rom: 0x341EB0 (near)
// Restore the saved menu buffer by DMA; registers and flags preserved.
void restore_menu_screen_two_call_core(Machine &m) {
    restore_menu_dma(m, kMenuScreenBackup, kScreenMap2, 0x800);
}
void restore_menu_screen_two(Machine &m) {
    restore_menu_screen_two_call_core(m);
    m.ret();
}

// Shared body of ROM 0x3422C2
// Position the direction-choice cursor from DS:D840; AX/BX/SI expose the chosen table entry.
void draw_direction_choice_cursor_core(Machine &m) {
    m.si() = 0x22e9;
    m.bx() = uint16_t(m.rd8(m.ds(), kSaveChoice - 3) * 2);
    m.ax() = m.rd16(m.cs(), uint16_t(m.si() + m.bx()));
    m.wr16(m.ds(), kMenuCursorAnchor, m.ax());
    m.bx() = m.ax();
    const MenuRegisters saved(m);
    m.ax() = 0;
    m.dx() = 0x3ffb;
    set_sprite_16x16_core(m);
    saved.restore(m);
    return;
}

// rom: 0x3422C2 (near)
void draw_direction_choice_cursor(Machine &m) {
    draw_direction_choice_cursor_core(m);
    m.ret();
}
// rom: 0x342361 (near)
// Draw direction DX's 2x2 icon at AX/BX. SI/DI retain the tilemap helper's results.
void draw_direction_choice_icon(Machine &m) {
    direction_choice_icon_core(m);
    m.ret();
}
// Shared body of ROM 0x3422F1
// Draw the available direction-choice icons using packed progress nibble A; all registers preserved.
void draw_direction_choices_core(Machine &m) {
    const MenuRegisters saved(m);
    m.ax() = 0;
    m.bx() = 0x70b;
    m.cx() = 0x608;
    draw_window_core(m);
    m.ds() = 0;
    m.cx() = uint16_t(m.rd8(m.ss(), (kPackedProgress + 0x5)) >> 4);
    constexpr uint16_t positions[] = {0x70c, 0x710, 0xa0c, 0xa10};
    constexpr uint16_t directions[] = {0, 1, 3, 2};
    if (m.cx() > 0 && m.cx() < 5)
        for (unsigned i = 0; i < m.cx(); ++i) {
            m.ax() = 1;
            m.bx() = positions[i];
            m.dx() = directions[i];
            direction_choice_icon_core(m);
        }
    saved.restore(m);
    return;
}

// rom: 0x3422F1 (near)
void draw_direction_choices(Machine &m) {
    draw_direction_choices_core(m);
    m.ret();
}
// Shared body of ROM 0x342524
// Refill the DS:SI-selected inventory item using the consumable at D840, returning CF on invalid targets.
void refill_inventory_item_core(Machine &m) {
    m.bx() = m.rd8(m.ds(), m.si());
    m.ax() = m.rd8(m.ds(), kMenuActionIndex);
    m.si() = uint16_t(kInventory + m.ax() * 2);
    m.di() = uint16_t(kInventory + m.bx() * 2);
    m.ax() = uint16_t(m.ax() * 2);
    m.bx() = uint16_t(m.bx() * 2);
    auto item = m.rd8(m.ds(), m.di());
    if (item == 10 || item == 255 || m.rd8(m.ds(), uint16_t(m.di() + 1)) == 255) {
        m.flags() |= emu::CF;
    } else {
        m.ax() = 0x9000;
        m.es() = m.ax();
        m.bx() = m.rd16(m.es(), uint16_t(0x41e0 + item * 2));
        m.set_al(table_at(m, Table::item_records, m.es(), uint16_t(m.bx() + 3))); // uses
        m.wr8(m.ds(), uint16_t(m.di() + 1), m.al());
        auto uses_remaining = uint8_t(m.rd8(m.ds(), uint16_t(m.si() + 1)) - 1);
        m.wr8(m.ds(), uint16_t(m.si() + 1), uses_remaining);
        if (uses_remaining == 0)
            m.wr16(m.ds(), m.si(), 0xffff);
        m.flags() &= uint16_t(~emu::CF);
    }
    return;
}

// rom: 0x342524 (near)
void refill_inventory_item(Machine &m) {
    refill_inventory_item_core(m);
    m.ret();
}

namespace {
void draw_tower_destination_core(Machine &m) {
    const auto saved_ax = m.ax(), saved_bx = m.bx(), saved_cx = m.cx(), saved_dx = m.dx(), ds = m.ds();
    if (has_value_tokens(m, TextKey::draw_tower_destination_1)) { // the floor is in the label: redraw it
        const auto si = m.si();
        TextValues values;
        values.set(U"floor", decimal_text(m.rd8(0, kSaveChoice + 8), 2));
        const TextPtr text_ptr = text(m, TextKey::draw_tower_destination_1);
        m.ax() = 1;
        m.bx() = 0x80b;
        m.ds() = text_ptr.segment;
        m.si() = text_ptr.offset;
        m.cx() = 7;
        draw_text_n_white_core(m);
        text_done(m, TextKey::draw_tower_destination_1);
        m.ax() = saved_ax;
        m.bx() = saved_bx;
        m.cx() = saved_cx;
        m.dx() = saved_dx;
        m.ds() = ds;
        m.si() = si;
        return;
    }
    m.ds() = 0;
    m.dx() = m.rd8(m.ds(), kSaveChoice + 8);
    decimal_to_bcd_clamped_core(m);
    m.cx() = 2;
    draw_bcd_number_core(m);
    m.ax() = saved_ax;
    m.bx() = saved_bx;
    m.cx() = saved_cx;
    m.dx() = saved_dx;
    m.ds() = ds;
}
void choice_cursor_core(Machine &m, uint16_t selection, uint16_t table) {
    m.bx() = uint16_t(m.rd8(m.ds(), selection) * 2);
    m.si() = table;
    m.ax() = m.rd16(m.cs(), uint16_t(m.bx() + table));
    m.wr16(m.ds(), kMenuCursorAnchor, m.ax());
    m.bx() = m.ax();
    const MenuRegisters saved(m);
    m.ax() = 0;
    m.dx() = 0x3ffb;
    set_sprite_16x16_core(m);
    saved.restore(m);
}
void draw_item_target_summary_core(Machine &m) {
    if (m.rd8(m.ss(), m.si()) == 255)
        return;
    auto draw_member_str = [&](uint16_t source, uint16_t count, bool rom) {
        const auto saved_ax = m.ax(), saved_bx = m.bx(), saved_si = m.si(), ds = m.ds();
        m.si() = source;
        m.cx() = count;
        if (rom)
            m.ds() = m.cs();
        draw_text_n_white_core(m);
        m.ax() = saved_ax;
        m.bx() = saved_bx;
        m.si() = saved_si;
        m.ds() = ds;
    };
    auto draw_member_str_key = [&](TextKey key, uint16_t count) {
        const auto saved_ax = m.ax(), saved_bx = m.bx(), saved_si = m.si(), ds = m.ds();
        const TextPtr text_ptr = text(m, key);
        m.si() = text_ptr.offset;
        m.cx() = count;
        m.ds() = text_ptr.segment;
        draw_text_n_white_core(m);
        text_done(m, key);
        m.ax() = saved_ax;
        m.bx() = saved_bx;
        m.si() = saved_si;
        m.ds() = ds;
    };
    auto number = [&](uint16_t offset, bool left, uint16_t count) {
        const auto saved_ax = m.ax(), saved_bx = m.bx(), saved_si = m.si(), ds = m.ds();
        m.si() = uint16_t(saved_si + offset);
        m.dx() = left ? m.rd8(m.ds(), m.si()) & 15 : m.rd16(m.ds(), m.si());
        decimal_to_bcd_clamped_core(m);
        m.ax() = saved_ax;
        m.bx() = saved_bx;
        if (left)
            draw_bcd_left_core(m);
        else {
            m.cx() = count;
            draw_bcd_number_core(m);
        }
        m.ax() = saved_ax;
        m.bx() = saved_bx;
        m.si() = saved_si;
        m.ds() = ds;
    };
    draw_member_str(uint16_t(m.si() + 1), 4, false);
    move_column(m, 5);
    // a language with value tokens (text_tokens.h) gets the numbers in its strings instead
    const bool heart_tokens = has_value_tokens(m, TextKey::draw_item_target_summary_1);
    const bool hp_tokens = has_value_tokens(m, TextKey::draw_item_target_summary_2);
    {
        TextValues values;
        if (heart_tokens)
            values.set(U"hearts", member_hearts(m, m.ds(), m.si()));
        draw_member_str_key(TextKey::draw_item_target_summary_1, 2);
    }
    move_column(m, 2);
    if (!heart_tokens)
        number(10, true, 0);
    move_row(m, 1);
    move_column(m, 249);
    {
        TextValues values;
        if (hp_tokens) {
            values.set(U"hp", member_hp(m, m.ds(), m.si()));
            values.set(U"maxhp", member_maxhp(m, m.ds(), m.si()));
        }
        draw_member_str_key(TextKey::draw_item_target_summary_2, 7);
    }
    move_column(m, 4);
    if (hp_tokens) {
        move_column(m, m.rd8(m.ds(), m.si()) == 0 ? 4 : 2);
        return;
    }
    number(6, false, 3);
    if (m.rd8(m.ds(), m.si()) == 0) {
        move_column(m, 4);
        number(8, false, 3);
    } else {
        move_column(m, 2);
        const auto saved_ax = m.ax(), saved_bx = m.bx(), saved_si = m.si(), ds = m.ds();
        auto race = m.rd8(m.ss(), saved_si);
        unsigned bit = 0;
        while (bit < 8 && !(race & 1)) {
            race >>= 1;
            ++bit;
        }
        m.si() = m.rd16(m.cs(), uint16_t(0x872c + bit * 2));
        m.ds() = m.cs();
        m.cx() = 3;
        draw_race_stat_core(m);
        m.ax() = saved_ax;
        m.bx() = saved_bx;
        m.si() = saved_si;
        m.ds() = ds;
    }
}
} // namespace
// Shared body of ROM 0x342652
// Draw the tower-destination dialog and selected floor; all registers preserved.
void draw_tower_destination_entry_core(Machine &m) {
    const MenuRegisters saved(m);
    frame(m, 0, 0x80a, 0x309);
    {
        const TextPtr text_ptr = text(m, TextKey::draw_tower_destination_1);
        m.ax() = 1;
        m.bx() = 0x80b;
        m.ds() = text_ptr.segment;
        m.si() = text_ptr.offset;
        m.cx() = 7;
        draw_text_n_white_core(m);
        text_done(m, TextKey::draw_tower_destination_1);
    }
    {
        const TextPtr text_ptr = text(m, TextKey::draw_tower_destination_2);
        m.ax() = 1;
        m.bx() = 0x90c;
        m.ds() = text_ptr.segment;
        m.si() = text_ptr.offset;
        m.cx() = 5;
        draw_text_n_white_core(m);
        text_done(m, TextKey::draw_tower_destination_2);
    }
    m.ax() = 1;
    m.bx() = 0x80f;
    draw_tower_destination_core(m);
    saved.restore(m);
    return;
}

// rom: 0x342652 (near)
void draw_tower_destination(Machine &m) {
    draw_tower_destination_entry_core(m);
    m.ret();
}
// Shared body of ROM 0x342698
// Draw the selected tower destination as two BCD digits; AX/BX/CX/DX/DS preserved.
void draw_tower_destination_number_core(Machine &m) {
    draw_tower_destination_core(m);
    return;
}

// rom: 0x342698 (near)
void draw_tower_destination_number(Machine &m) {
    draw_tower_destination_number_core(m);
    m.ret();
}
// Shared body of ROM 0x342878
// Draw the item-error tilemap; SI/DS preserved, other registers expose draw_tilemap results.
void draw_item_error_tiles_core(Machine &m) {
    const auto saved_si = m.si(), ds = m.ds();
    m.ds() = m.cs();
    m.ax() = 0;
    m.bx() = 0xe09;
    m.cx() = 0x20c;
    m.si() = 0x2892;
    draw_tilemap_core(m);
    m.si() = saved_si;
    m.ds() = ds;
    return;
}

// rom: 0x342878 (near)
void draw_item_error_tiles(Machine &m) {
    draw_item_error_tiles_core(m);
    m.ret();
}
// Shared body of ROM 0x3428C2
// Swap two item/count records AL and BL in DS:SI; AX/BX scaled, SI/DI retain pointers.
void swap_item_records_core(Machine &m) {
    m.ax() = uint16_t(m.al() * 2);
    m.bx() = uint16_t(uint8_t(m.bx()) * 2);
    m.di() = uint16_t(m.si() + m.bx());
    m.si() = uint16_t(m.si() + m.ax());
    auto source_item = m.rd16(m.ds(), m.si()), destination_item = m.rd16(m.ds(), m.di());
    m.wr16(m.ds(), m.si(), destination_item);
    m.wr16(m.ds(), m.di(), source_item);
    return;
}

// rom: 0x3428C2 (near)
void swap_item_records(Machine &m) {
    swap_item_records_core(m);
    m.ret();
}
// Shared body of ROM 0x3428D9
// Mark AL's item/count record at DS:SI empty, leaving AX scaled and SI at the record.
void erase_item_record_core(Machine &m) {
    m.ax() = uint16_t(m.al() * 2);
    m.si() = uint16_t(m.si() + m.ax());
    m.wr16(m.ds(), m.si(), 0xffff);
    return;
}

// rom: 0x3428D9 (near)
void erase_item_record(Machine &m) {
    erase_item_record_core(m);
    m.ret();
}
// Shared body of ROM 0x342941
// Position the refill-target cursor selected by D842, exposing table position in AX/BX.
void draw_refill_target_cursor_core(Machine &m) {
    choice_cursor_core(m, kSaveSlotSelection, 0x2968);
    return;
}

// rom: 0x342941 (near)
void draw_refill_target_cursor(Machine &m) {
    draw_refill_target_cursor_core(m);
    m.ret();
}
// Shared body of ROM 0x342978
// Position the party item-target cursor selected by D844, exposing table position in AX/BX.
void draw_item_target_cursor_core(Machine &m) {
    choice_cursor_core(m, kMenuThirdSelection, 0x299f);
    return;
}

// rom: 0x342978 (near)
void draw_item_target_cursor(Machine &m) {
    draw_item_target_cursor_core(m);
    m.ret();
}
// Shared body of ROM 0x3429A7
// Load party sprites and draw four item-target summaries; all registers preserved.
void draw_item_targets_core(Machine &m) {
    const MenuRegisters saved(m);
    load_party_menu_sprites_core(m);
    for (unsigned i = 0; i < 4; ++i)
        if (m.rd8(m.ss(), uint16_t(kGameState + i * 31)) != 255) {
            m.ax() = uint16_t(8 + i * 4);
            m.bx() = uint16_t(0x1878 + i * 0x1800);
            m.dx() = uint16_t(i * 0x204);
            set_sprite_16x16_core(m);
        }
    frame(m, 0, 0x20f, 0xe0e);
    m.ds() = 0;
    for (unsigned i = 0; i < 4; ++i) {
        m.ax() = 0;
        m.bx() = uint16_t(0x312 + i * 0x300);
        m.si() = uint16_t(kGameState + i * 31);
        draw_item_target_summary_core(m);
    }
    saved.restore(m);
    return;
}

// rom: 0x3429A7 (near)
void draw_item_targets(Machine &m) {
    draw_item_targets_core(m);
    m.ret();
}
// rom: 0x342A50 (near)
// Draw one party member's name, equipment count, HP and secondary stat for item targeting.
void draw_item_target_summary(Machine &m) {
    draw_item_target_summary_core(m);
    m.ret();
}
// Shared body of ROM 0x342FE2
// Count nonempty members in DL, preserving DH and AX; SI ends after the party, CX zero.
void count_party_members_core(Machine &m) {
    m.dx() &= 0xff00;
    m.si() = kGameState;
    m.cx() = 4;
    do {
        if (m.rd8(m.ss(), m.si()) != 255)
            m.dx() = uint16_t((m.dx() & 0xff00) | uint8_t(m.dx() + 1));
        m.si() = uint16_t(m.si() + 31);
    } while (--m.cx());
    return;
}

// rom: 0x342FE2 (near)
void count_party_members(Machine &m) {
    count_party_members_core(m);
    m.ret();
}
// Shared body of ROM 0x34300E
// Find last nonempty member from slot 3 in DL/SI. All-empty underflows DL to FF and SI before party.
void find_last_party_member_core(Machine &m) {
    m.dx() = uint16_t((m.dx() & 0xff00) | 3);
    m.si() = uint16_t(kGameState + 93);
    m.cx() = 4;
    do {
        if (m.rd8(m.ss(), m.si()) != 255)
            break;
        m.dx() = uint16_t((m.dx() & 0xff00) | uint8_t(m.dx() - 1));
        m.si() = uint16_t(m.si() - 31);
    } while (--m.cx());
    return;
}

// rom: 0x34300E (near)
void find_last_party_member(Machine &m) {
    find_last_party_member_core(m);
    m.ret();
}

namespace {
uint16_t misc_alu_flags(Machine &m, uint16_t left, uint16_t right, unsigned width, char op) {
    unsigned mask = width == 8 ? 255 : 65535, sign = (mask + 1) / 2;
    unsigned result = op == '&'   ? (left & right)
                      : op == '+' ? unsigned(left) + right
                                  : unsigned(left) - right;
    auto masked_result = uint16_t(result & mask);
    auto flags = uint16_t(m.flags() & ~(emu::CF | emu::OF | emu::AF | emu::ZF | emu::SF | emu::PF));
    if (op != '&') {
        if (op == '+' ? result > mask : left < right)
            flags |= emu::CF;
        if ((op == '+' ? ~(left ^ right) : (left ^ right)) & (left ^ masked_result) & sign)
            flags |= emu::OF;
        if ((left ^ right ^ masked_result) & 16)
            flags |= emu::AF;
    }
    if (!masked_result)
        flags |= emu::ZF;
    if (masked_result & sign)
        flags |= emu::SF;
    unsigned bits = 0;
    for (unsigned i = 0; i < 8; ++i)
        bits += (masked_result >> i) & 1;
    if (!(bits & 1))
        flags |= emu::PF;
    m.flags() = flags;
    return masked_result;
}
bool misc_cmp(Machine &m, uint16_t left, uint16_t right, unsigned width = 8) {
    misc_alu_flags(m, left, right, width, '-');
    return left == right;
}
bool misc_test(Machine &m, uint16_t left, uint16_t right) {
    return misc_alu_flags(m, left, right, 8, '&') != 0;
}
void misc_carry(Machine &m, bool carry) {
    m.flags() = uint16_t((m.flags() & ~emu::CF) | (carry ? emu::CF : 0));
}
// Probe at most four party slots, skipping empty slots and optionally dead/stoned members.
void advance_member_selection_core(Machine &m, int step, bool require_conscious) {
    for (unsigned i = 0; i < 4; ++i) {
        auto index = m.rd8(m.ss(), m.si());
        auto status = m.rd8(m.ss(), uint16_t(kGameState + index * 31));
        if (status != 255 && (!require_conscious || !(status & 3)))
            break;
        m.wr8(m.ss(), m.si(), uint8_t(index + step) & 3);
    }
}
// Draw the selected member's equipped items and remaining-use counts.
void equipment_rows_core(Machine &m) {
    if (m.rd8(m.ss(), uint16_t(m.si() + 10)) & 32) {
        const MenuRegisters saved(m);
        m.ds() = m.cs();
        m.cx() = 4;
        move_column(m, 9);
        do {
            const auto saved_ax = m.ax(), saved_bx = m.bx(), saved_cx = m.cx();
            const TextPtr text_ptr = text(m, TextKey::draw_member_equipment_rows_1);
            m.ds() = text_ptr.segment;
            m.si() = text_ptr.offset;
            draw_text_core(m);
            text_done(m, TextKey::draw_member_equipment_rows_1);
            m.ax() = saved_ax;
            m.bx() = saved_bx;
            m.cx() = saved_cx;
            move_row(m, 1);
        } while (--m.cx());
        saved.restore(m);
    }
    m.si() = uint16_t(m.si() + 15);
    draw_item_entry_rows_core(m);
}
// Draw one selected member's status, race, HP, stats and equipment, retaining the draw-call registers.
void selected_member_details_core(Machine &m, bool equipment) {
    auto slot = m.rd8(m.ss(), kCreationRace);
    m.ax() = 24;
    m.bx() = equipment ? 0x458 : 0x2020;
    m.cx() = uint16_t(slot << 9);
    m.dx() = uint16_t(slot * 4 + m.cx());
    set_sprite_16x16_core(m);
    m.si() = uint16_t(kGameState + slot * 31);
    m.dx() = uint16_t((m.dx() & 0xff00) | 31);
    const auto member = m.si();
    m.ds() = 0;
    auto draw_member_field = [&](uint16_t source, uint16_t page, uint16_t pos, uint16_t count, bool rom) {
        const auto ds = m.ds();
        m.si() = source;
        m.ax() = page;
        m.bx() = pos;
        m.cx() = count;
        if (rom)
            m.ds() = m.cs();
        draw_text_n_white_core(m);
        m.ds() = ds;
        m.si() = member;
    };
    auto draw_member_field_key = [&](TextKey key, uint16_t page, uint16_t pos, uint16_t count) {
        const auto ds = m.ds();
        const TextPtr text_ptr = text(m, key);
        m.si() = text_ptr.offset;
        m.ax() = page;
        m.bx() = pos;
        m.cx() = count;
        m.ds() = text_ptr.segment;
        draw_text_n_white_core(m);
        text_done(m, key);
        m.ds() = ds;
        m.si() = member;
    };
    auto number = [&](uint16_t offset, uint16_t page, uint16_t pos, uint16_t count, bool byte, bool left) {
        const auto ds = m.ds();
        m.si() = uint16_t(member + offset);
        m.dx() = byte ? m.rd8(m.ss(), m.si()) : m.rd16(m.ss(), m.si());
        if (left)
            m.dx() &= 15;
        if (byte && !left && m.dx() >= 99)
            m.dx() = 99;
        decimal_to_bcd_clamped_core(m);
        m.ax() = page;
        m.bx() = pos;
        if (left)
            draw_bcd_left_core(m);
        else {
            m.cx() = count;
            draw_bcd_number_core(m);
        }
        m.si() = member;
        m.ds() = ds;
    };
    auto page = uint16_t(equipment ? 1 : 0), row = uint16_t(equipment ? 0x300 : 0x400),
         col = uint16_t(equipment ? 4 : 8);
    draw_member_field(uint16_t(member + 1), page, uint16_t(row + col), 4, false);
    m.set_al(m.rd8(m.ss(), uint16_t(member + 5)));
    lookup_name_record_core(m);
    m.ax() = page;
    m.bx() = uint16_t(row + col + 5);
    draw_text_core(m);
    m.si() = member;
    // a language with value tokens (text_tokens.h) gets the numbers in its strings instead
    const TextKey hp_key =
        equipment ? TextKey::draw_equipment_edit_member_1 : TextKey::draw_equipment_review_member_1;
    const TextKey heart_key =
        equipment ? TextKey::draw_equipment_edit_member_2 : TextKey::draw_equipment_review_member_2;
    const bool hp_tokens = has_value_tokens(m, hp_key), heart_tokens = has_value_tokens(m, heart_key);
    {
        TextValues values;
        if (hp_tokens) {
            values.set(U"hp", member_hp(m, m.ss(), member));
            values.set(U"maxhp", member_maxhp(m, m.ss(), member));
        }
        draw_member_field_key(hp_key, page, uint16_t(row + 0x100 + col), 7);
    }
    if (!hp_tokens) {
        number(6, page, uint16_t(row + 0x100 + col + 4), 3, false, false);
        number(8, page, uint16_t(row + 0x100 + col + 8), 3, false, false);
    }
    if (m.rd8(m.ss(), member) != 0 && !hp_tokens) {
        const auto ds = m.ds();
        unsigned bit = 0;
        auto race = m.rd8(m.ss(), member);
        while (bit < 8 && !(race & 1)) {
            race >>= 1;
            ++bit;
        }
        m.ds() = m.cs();
        m.si() = m.rd16(m.cs(), uint16_t(0x872c + bit * 2));
        m.ax() = page;
        m.bx() = uint16_t(row + 0x100 + col + 6);
        m.cx() = 3;
        draw_race_stat_core(m);
        m.ds() = ds;
        m.si() = member;
    }
    {
        TextValues values;
        if (heart_tokens)
            values.set(U"hearts", member_hearts(m, m.ss(), member));
        draw_member_field_key(heart_key, page, uint16_t(row + 0x100 + col + 10), 2);
    }
    if (!heart_tokens)
        number(10, page, uint16_t(row + 0x100 + col + 12), 0, true, true);
    constexpr TextKey reviewLabels[] = {
        TextKey::draw_equipment_review_member_3, TextKey::draw_equipment_review_member_5,
        TextKey::draw_equipment_review_member_7, TextKey::draw_equipment_review_member_9};
    constexpr TextKey reviewSeparators[] = {
        TextKey::draw_equipment_review_member_4, TextKey::draw_equipment_review_member_6,
        TextKey::draw_equipment_review_member_8, TextKey::draw_equipment_review_member_10};
    constexpr TextKey editLabels[] = {
        TextKey::draw_equipment_edit_member_3, TextKey::draw_equipment_edit_member_5,
        TextKey::draw_equipment_edit_member_7, TextKey::draw_equipment_edit_member_9};
    constexpr TextKey editSeparators[] = {
        TextKey::draw_equipment_edit_member_4, TextKey::draw_equipment_edit_member_6,
        TextKey::draw_equipment_edit_member_8, TextKey::draw_equipment_edit_member_10};
    for (unsigned i = 0; i < 4; ++i) {
        auto labelPos = uint16_t(equipment ? 0x113 + i * 0x100 : 0x711 + i * 0x200),
             valuePos = uint16_t(equipment ? labelPos + 6 : labelPos + 0x104);
        draw_member_field_key(equipment ? editLabels[i] : reviewLabels[i], 0, labelPos, 4);
        const TextKey separator = equipment ? editSeparators[i] : reviewSeparators[i];
        const bool tokens = has_value_tokens(m, separator);
        {
            TextValues values;
            if (tokens)
                values.set(U"value",
                           decimal_text(std::min<unsigned>(m.rd8(m.ss(), uint16_t(member + 11 + i)), 99), 2));
            draw_member_field_key(separator, 0, uint16_t(equipment ? labelPos + 4 : labelPos + 0x102), 1);
        }
        if (!tokens)
            number(uint16_t(11 + i), 0, valuePos, 2, true, false);
    }
    m.ax() = 0;
    m.ds() = 0;
    m.cx() = 8;
    m.bx() = 0x703;
    m.si() = member;
    equipment_rows_core(m);
    m.ds() = 0x9000;
    for (unsigned i = 0; i < 2; ++i) {
        m.ax() = equipment ? 1 : 0;
        m.bx() = uint16_t((equipment ? 0x301 : 0x401) + i * 26);
        m.cx() = 0x202;
        if (equipment && m.rd8(m.ss(), kNamePage) == 0) {
            m.dx() = 0x1ff;
            fill_screen_rect_core(m);
        } else {
            m.si() = i ? 0x3068 : 0x3078;
            draw_tilemap_core(m);
        }
    }
}
void draw_equipment_inventory_core(Machine &m) {
    const bool key_items = m.rd8(m.ss(), kNameInsertionIndex) == 1;
    const TextKey key = key_items ? TextKey::draw_equipment_inventory_2 : TextKey::draw_equipment_inventory_1;
    const TextPtr text_ptr = text(m, key);
    m.ax() = 0;
    m.bx() = 0x610;
    m.ds() = text_ptr.segment;
    m.si() = text_ptr.offset;
    draw_text_core(m);
    text_done(m, key);
    m.ds() = 0;
    m.ax() = 0;
    m.cx() = m.rd8(m.ss(), kNameInsertionIndex) == 1 ? 9 : 8;
    m.bx() = 0x710;
    m.si() = m.cx() == 9 ? kKeyItems : kInventory;
    draw_item_entry_rows_core(m);
}
// Build the equipment review/edit screen and its selected member/inventory panes.
void equipment_screen_core(Machine &m, bool equipment) {
    const MenuRegisters saved(m);
    load_party_menu_sprites_core(m);
    m.ds() = 0;
    for (unsigned i = 0; i < 4; ++i) {
        m.ax() = uint16_t(8 + i * 4);
        m.bx() = 0x9600;
        m.dx() = uint16_t(i * 0x204);
        set_sprite_16x16_core(m);
    }
    frame(m, 0, 1, 0x121c);
    frame(m, 0, equipment ? 1 : 0x201, equipment ? 0x61c : 0xe1c);
    frame(m, 0, 1, 0x307);
    if (equipment)
        frame(m, 0, 0x601, 0xa0e);
    frame(m, 0, 0x1001, 0x21c);
    menu_text(m, equipment ? TextKey::draw_equipment_edit_screen_1 : TextKey::draw_equipment_review_screen_1,
              0, equipment ? 0x103 : 0x102);
    if (!equipment)
        menu_text(m, TextKey::draw_equipment_review_screen_2, 1, 9);
    m.ds() = 0x9000;
    m.si() = 0x3068;
    m.bx() = 0x31b;
    m.cx() = 0x202;
    m.ax() = 1;
    selected_member_details_core(m, equipment);
    if (equipment)
        draw_equipment_inventory_core(m);
    saved.restore(m);
}
// Replace an equipped item, checking duplicate categories and saturating its stat bonuses. CF is the result.
void replace_equipment_core(Machine &m) {
    misc_cmp(m, m.si(), (kMember0Equipment + 0x1F), 16);
    if (m.si() < (kMember0Equipment + 0x1F))
        m.bp() = kGameState;
    else {
        misc_cmp(m, m.si(), (kMember0Equipment + 0x3E), 16);
        if (m.si() < (kMember0Equipment + 0x3E))
            m.bp() = kMember1Record;
        else {
            misc_cmp(m, m.si(), (kMember0Equipment + 0x5D), 16);
            m.bp() = m.si() < (kMember0Equipment + 0x5D) ? kMember2Record : kMember3Record;
        }
    }
    m.dx() = m.bp();
    auto source_item = m.rd8(m.ss(), m.si());
    misc_cmp(m, source_item, 0x80);
    if (source_item >= 0x80) {
        misc_cmp(m, source_item, 0xfc);
        if (source_item < 0xfc) {
            misc_carry(m, true);
            return;
        }
    }
    m.ax() = 0x9000;
    m.es() = m.ax();
    m.bx() = uint16_t((m.bx() & 0xff00) | m.rd8(m.ss(), m.di()));
    if (!misc_cmp(m, m.rd8(m.ss(), uint16_t(m.di() + 1)), 255)) {
        m.bx() = m.rd16(m.es(), uint16_t(0x41e0 + uint8_t(m.bx()) * 2));
        m.set_ah(table_at(m, Table::item_records, m.es(), m.bx())); // flags
        if (misc_test(m, m.ah(), 0x3c)) {
            m.bp() = uint16_t(m.bp() + 15);
            m.cx() = 8;
            m.set_al(0);
            do {
                if (!misc_cmp(m, m.si(), m.bp(), 16)) {
                    m.bx() = uint16_t((m.bx() & 0xff00) | m.rd8(m.ss(), m.bp()));
                    if (!misc_cmp(m, m.rd8(m.ss(), uint16_t(m.bp() + 1)), 255)) {
                        m.bx() = m.rd16(m.es(), uint16_t(0x41e0 + uint8_t(m.bx()) * 2));
                        m.bx() = uint16_t((m.bx() & 255) |
                                          (table_at(m, Table::item_records, m.es(), m.bx()) << 8)); // flags
                        auto category = uint8_t(m.bx() >> 8) & 0x3c;
                        m.bx() = uint16_t((m.bx() & 255) | (category << 8));
                        m.set_al(uint8_t(m.al() | category));
                    }
                }
                m.bp() = uint16_t(misc_alu_flags(m, m.bp(), 2, 16, '+'));
            } while (--m.cx());
            if (misc_test(m, m.ah(), m.al())) {
                misc_carry(m, true);
                return;
            }
        }
    }
    // Remove old bonuses, then add new ones; item-category bits 2..5 may not duplicate another occupied slot.
    auto adjust = [&](bool add, uint16_t record) {
        m.bp() = m.dx();
        m.bx() = uint16_t((m.bx() & 0xff00) | m.rd8(m.ss(), record));
        if (misc_cmp(m, m.rd8(m.ss(), uint16_t(record + 1)), 255))
            return;
        m.bx() = m.rd16(m.es(), uint16_t(0x41e0 + uint8_t(m.bx()) * 2));
        m.set_ah(table_at(m, Table::item_records, m.es(), m.bx())); // flags
        if (!misc_test(m, m.ah(), 0x3c))
            return;
        // Original-game quirk kept: each byte stat adjustment saturates separately, so capped bonuses are not
        // reversible.
        auto stat = [&](uint16_t offset, uint8_t bonus) {
            auto previous_stat = m.rd8(m.ss(), uint16_t(m.bp() + offset));
            m.set_al(uint8_t(misc_alu_flags(m, previous_stat, bonus, 8, add ? '+' : '-')));
            if (m.flags() & emu::CF)
                m.set_al(add ? 255 : 0);
            m.wr8(m.ss(), uint16_t(m.bp() + offset), m.al());
        };
        // Armor power changes defense; passive kinds 1/2/3 add 10 strength/agility/magic, other nonzero kinds
        // add strength+agility.
        stat(12, table_at(m, Table::item_records, m.es(), uint16_t(m.bx() + 4))); // power
        if (misc_test(m, table_at(m, Table::item_records, m.es(), m.bx()), 0x40))
            return;
        auto passive_bonus = table_at(m, Table::item_records, m.es(), uint16_t(m.bx() + 5)); // passive_bits
        if (misc_cmp(m, passive_bonus, 0))
            return;
        if (misc_cmp(m, passive_bonus, 1)) {
            stat(11, 10);
            return;
        }
        if (misc_cmp(m, passive_bonus, 2)) {
            stat(13, 10);
            return;
        }
        if (misc_cmp(m, passive_bonus, 3)) {
            stat(14, 10);
            return;
        }
        stat(11, 10);
        stat(13, 10);
    };
    adjust(false, m.si());
    adjust(true, m.di());
    misc_carry(m, false);
}
} // namespace
// Shared body of ROM 0x343688
// Position the refill cursor from equipment/inventory mode and selection.
void draw_equipment_refill_cursor_core(Machine &m) {
    bool inventory = m.rd8(m.ss(), kCreationLeader) != 0;
    choice_cursor_core(m, inventory ? kSaveChoice : kSaveSlotSelection, inventory ? 0x36d0 : 0x36c0);
    return;
}

// rom: 0x343688 (near)
void draw_equipment_refill_cursor(Machine &m) {
    draw_equipment_refill_cursor_core(m);
    m.ret();
}
// Shared body of ROM 0x3436E2
// Position the equipment action cursor; selection and cursor state use SS.
void draw_equipment_action_cursor_core(Machine &m) {
    const auto ds = m.ds();
    m.ds() = m.ss();
    bool inventory = m.rd8(m.ss(), kCreationLeader) == 1;
    choice_cursor_core(m, inventory ? kSaveChoice : kSaveSlotSelection, inventory ? 0x3737 : 0x3727);
    m.ds() = ds;
    return;
}

// rom: 0x3436E2 (near)
void draw_equipment_action_cursor(Machine &m) {
    draw_equipment_action_cursor_core(m);
    m.ret();
}
// Shared body of ROM 0x343749
// Draw equipment-review screen with selected member details; preserve all registers.
void draw_equipment_review_screen_core(Machine &m) {
    equipment_screen_core(m, false);
    return;
}

// rom: 0x343749 (near)
void draw_equipment_review_screen(Machine &m) {
    draw_equipment_review_screen_core(m);
    m.ret();
}
// rom: 0x343822 (near)
// Draw the selected member's stats, equipment and walking icon; expose final tilemap registers.
void draw_equipment_review_member(Machine &m) {
    selected_member_details_core(m, false);
    m.ret();
}
// Shared body of ROM 0x343A93
// Replace the left equipment marker with a CS-relative tilemap; DS preserved.
void draw_equipment_left_marker_core(Machine &m) {
    const auto ds = m.ds();
    m.ds() = m.cs();
    m.si() = 0x3aa9;
    m.bx() = 0x401;
    m.cx() = 0x202;
    m.ax() = 0;
    draw_tilemap_core(m);
    m.ds() = ds;
    return;
}

// rom: 0x343A93 (near)
void draw_equipment_left_marker(Machine &m) {
    draw_equipment_left_marker_core(m);
    m.ret();
}
// Shared body of ROM 0x343AB1
// Replace the right equipment marker with a CS-relative tilemap; DS preserved.
void draw_equipment_right_marker_core(Machine &m) {
    const auto ds = m.ds();
    m.ds() = m.cs();
    m.si() = 0x3ac7;
    m.bx() = 0x41b;
    m.cx() = 0x202;
    m.ax() = 0;
    draw_tilemap_core(m);
    m.ds() = ds;
    return;
}

// rom: 0x343AB1 (near)
void draw_equipment_right_marker(Machine &m) {
    draw_equipment_right_marker_core(m);
    m.ret();
}
// Shared body of ROM 0x343ACF
// Draw the inventory refill target window; DS zero, item drawing leftovers retained.
void draw_refill_inventory_core(Machine &m) {
    m.ds() = 0;
    frame(m, 0, 0x50f, 0xb0e);
    {
        const TextPtr text_ptr = text(m, TextKey::draw_refill_inventory_1);
        m.ds() = text_ptr.segment;
        m.si() = text_ptr.offset;
        m.ax() = 0;
        m.bx() = 0x610;
        m.cx() = 4;
        draw_text_n_white_core(m);
        text_done(m, TextKey::draw_refill_inventory_1);
    }
    m.ds() = 0;
    m.ax() = 0;
    m.cx() = 8;
    m.bx() = 0x711;
    m.si() = kInventory;
    draw_item_entry_rows_core(m);
    return;
}

// rom: 0x343ACF (near)
void draw_refill_inventory(Machine &m) {
    draw_refill_inventory_core(m);
    m.ret();
}
// Shared body of ROM 0x343B1F
// Draw refill-target instructions, preserving DS.
void draw_refill_instructions_core(Machine &m) {
    const auto ds = m.ds();
    menu_text(m, TextKey::draw_refill_instructions_1, 1, 0x1002);
    m.ds() = ds;
    return;
}

// rom: 0x343B1F (near)
void draw_refill_instructions(Machine &m) {
    draw_refill_instructions_core(m);
    m.ret();
}
// Shared body of ROM 0x343B32
// Draw tower-destination instructions, preserving DS.
void draw_tower_instructions_core(Machine &m) {
    const auto ds = m.ds();
    menu_text(m, TextKey::draw_tower_instructions_1, 1, 0x1002);
    m.ds() = ds;
    return;
}

// rom: 0x343B32 (near)
void draw_tower_instructions(Machine &m) {
    draw_tower_instructions_core(m);
    m.ret();
}
// Shared body of ROM 0x343B45
// Advance SS:SI selection to a nonempty conscious member; four probes, registers preserved.
void select_next_equipment_member_core(Machine &m) {
    advance_member_selection_core(m, 1, true);
    return;
}

// rom: 0x343B45 (near)
void select_next_equipment_member(Machine &m) {
    select_next_equipment_member_core(m);
    m.ret();
}
// Shared body of ROM 0x343B76
// Retreat SS:SI selection to a nonempty conscious member; four probes, registers preserved.
void select_previous_equipment_member_core(Machine &m) {
    advance_member_selection_core(m, -1, true);
    return;
}

// rom: 0x343B76 (near)
void select_previous_equipment_member(Machine &m) {
    select_previous_equipment_member_core(m);
    m.ret();
}
// Shared body of ROM 0x3442D3
// Check/adjust stats for moving equipment across lists, preserving SI/DI, returning CF on rejection.
void prepare_equipment_transfer_core(Machine &m) {
    const auto saved_si = m.si(), saved_di = m.di();
    m.set_al(m.rd8(m.ss(), kNamePage));
    if (!misc_cmp(m, m.al(), m.rd8(m.ss(), kCreationLeader))) {
        misc_cmp(m, m.si(), kInventory, 16);
        if (m.si() >= kInventory) {
            m.si() = saved_di;
            m.di() = saved_si;
        }
        replace_equipment_core(m);
    }
    bool carry = m.flags() & emu::CF;
    m.si() = saved_si;
    m.di() = saved_di;
    misc_carry(m, carry);
    return;
}

// rom: 0x3442D3 (near)
void prepare_equipment_transfer(Machine &m) {
    prepare_equipment_transfer_core(m);
    m.ret();
}
// Shared body of ROM 0x3442F7
// Validate special-key-item transfers across equipment/inventory lists. SI/DI preserved; CF is rejection.
void validate_key_item_transfer_core(Machine &m) {
    const auto saved_si = m.si(), saved_di = m.di();
    m.set_al(m.rd8(m.ss(), kNamePage));
    bool reject = false;
    if (!misc_cmp(m, m.al(), m.rd8(m.ss(), kCreationLeader))) {
        misc_cmp(m, m.si(), kInventory, 16);
        if (m.si() >= kInventory) {
            m.si() = saved_di;
            m.di() = saved_si;
        }
        auto special = [&](uint16_t item_address) {
            auto item = m.rd8(m.ss(), item_address);
            if (misc_cmp(m, m.rd8(m.ss(), uint16_t(item_address + 1)), 255))
                return true;
            if (misc_cmp(m, item, 0x11) || misc_cmp(m, item, 0x12) || misc_cmp(m, item, 0x13))
                return true;
            return false;
        };
        if (misc_cmp(m, m.rd8(m.ss(), kNameInsertionIndex), 0)) {
            reject = !misc_cmp(m, m.rd8(m.ss(), uint16_t(m.si() + 1)), 255) &&
                     (misc_cmp(m, m.rd8(m.ss(), m.si()), 0x11) || misc_cmp(m, m.rd8(m.ss(), m.si()), 0x12) ||
                      misc_cmp(m, m.rd8(m.ss(), m.si()), 0x13));
        } else
            reject = !special(m.si()) || !special(m.di());
    }
    m.si() = saved_si;
    m.di() = saved_di;
    misc_carry(m, reject);
    return;
}

// rom: 0x3442F7 (near)
void validate_key_item_transfer(Machine &m) {
    validate_key_item_transfer_core(m);
    m.ret();
}
// Shared body of ROM 0x344371
// Swap the SS:SI/SS:DI item records, preserving all registers and flags.
void swap_equipment_records_core(Machine &m) {
    auto source_item = m.rd16(m.ss(), m.si()), destination_item = m.rd16(m.ss(), m.di());
    m.wr16(m.ss(), m.si(), destination_item);
    m.wr16(m.ss(), m.di(), source_item);
    return;
}

// rom: 0x344371 (near)
void swap_equipment_records(Machine &m) {
    swap_equipment_records_core(m);
    m.ret();
}
// Shared body of ROM 0x34437E
// Reject duplicate equipment categories or update saturated stat bonuses for removal/replacement; CF result.
void replace_equipment_stats_core(Machine &m) {
    replace_equipment_core(m);
    return;
}

// rom: 0x34437E (near)
void replace_equipment_stats(Machine &m) {
    replace_equipment_stats_core(m);
    m.ret();
}
// Shared body of ROM 0x34456C
// Position or hide the equipment-selection cursor; SS state and CS table determine AX/BX/SI.
void draw_equipment_selection_cursor_core(Machine &m) {
    const auto ds = m.ds();
    m.ds() = m.ss();
    if (m.rd8(m.ss(), kCreationLeader) == 1)
        choice_cursor_core(m, kSaveChoice, 0x45d0);
    else if (m.rd8(m.ss(), kNameText) == 1) {
        m.bx() &= 255;
        m.ax() = 0xe0;
        m.wr16(m.ss(), kMenuCursorAnchor, m.ax());
        m.bx() = m.ax();
        const MenuRegisters saved(m);
        m.ax() = 0;
        m.dx() = 0x3ffb;
        set_sprite_16x16_core(m);
        saved.restore(m);
    } else
        choice_cursor_core(m, kSaveSlotSelection, 0x45be);
    m.ds() = ds;
    return;
}

// rom: 0x34456C (near)
void draw_equipment_selection_cursor(Machine &m) {
    draw_equipment_selection_cursor_core(m);
    m.ret();
}
// Shared body of ROM 0x3445E2
// Derive equipment slot bounds in DL/DH and adjust D842 for a restricted member; SI points at member.
void equipment_slot_bounds_core(Machine &m) {
    m.ax() = uint16_t(m.rd8(m.ss(), kCreationRace) * 31);
    m.si() = uint16_t(kGameState + m.ax());
    m.dx() = uint16_t((m.dx() & 0xff00) | 31);
    m.set_al(m.rd8(m.ss(), uint16_t(m.si() + 10)));
    m.bx() = uint16_t((m.bx() & 0xff00) | m.rd8(m.ss(), kSaveSlotSelection));
    if (m.al() & 64) {
        m.dx() = 0x808;
        m.wr8(m.ss(), kNameText, 1);
    } else {
        m.dx() = uint16_t(0x700 | ((m.al() & 32) ? 4 : 0));
        m.wr8(m.ss(), kNameText, 0);
        if ((m.al() & 32) && m.rd8(m.ss(), kSaveSlotSelection) < 4)
            m.wr8(m.ss(), kSaveSlotSelection, 4);
    }
    return;
}

// rom: 0x3445E2 (near)
void equipment_slot_bounds(Machine &m) {
    equipment_slot_bounds_core(m);
    m.ret();
}
// Shared body of ROM 0x344633
// Draw equipment-edit screen, member details and inventory; all registers preserved.
void draw_equipment_edit_screen_core(Machine &m) {
    equipment_screen_core(m, true);
    return;
}

// rom: 0x344633 (near)
void draw_equipment_edit_screen(Machine &m) {
    draw_equipment_edit_screen_core(m);
    m.ret();
}
// Shared body of ROM 0x344706
void select_next_party_member_core(Machine &m) {
    advance_member_selection_core(m, 1, false);
    return;
}

// rom: 0x344706 (near)
// Advance SS:SI to the next nonempty party slot; four probes, registers preserved.
void select_next_party_member(Machine &m) {
    select_next_party_member_core(m);
    m.ret();
}
// Shared body of ROM 0x34472F
void select_previous_party_member_core(Machine &m) {
    advance_member_selection_core(m, -1, false);
    return;
}

// rom: 0x34472F (near)
// Retreat SS:SI to the previous nonempty party slot; four probes, registers preserved.
void select_previous_party_member(Machine &m) {
    select_previous_party_member_core(m);
    m.ret();
}
// rom: 0x344758 (near)
// Draw selected member details for the equipment editor; final tilemap registers retained.
void draw_equipment_edit_member(Machine &m) {
    selected_member_details_core(m, true);
    m.ret();
}

namespace {
// Compute unlock index = group*30 + row*6 + column and leave DL = 6.
void creation_type_index_core(Machine &m) {
    m.ax() = uint16_t(m.rd8(m.ss(), kCreationLeader) * 30 + m.rd8(m.ss(), kMenuThirdSelection) * 6 +
                      m.rd8(m.ss(), kSaveChoice));
    m.bx() = m.ax();
    m.dx() = uint16_t((m.dx() & 0xff00) | 6);
}
// Test the creation unlock bit for types 0..149; later type indices are always selectable.
bool creation_type_unlocked_core(Machine &m) {
    if (m.al() > 149) {
        misc_cmp(m, m.al(), 149);
        return true;
    }
    // Unlock bits share the event-flag array: byte = type/8, mask selected by type%8.
    auto type = m.al();
    auto mask = m.rd8(m.cs(), uint16_t(0x5f51 + (type & 7)));
    return misc_test(m, m.rd8(m.ss(), uint16_t(kEventFlags + (type >> 3))), mask);
}
// Advance or retreat the creation selector with wraparound, skipping at most six locked choices.
void advance_creation_type_core(Machine &m, bool forward) {
    const MenuRegisters saved(m);
    for (unsigned i = 0; i < 6; ++i) {
        const auto dx = m.dx(), si = m.si();
        creation_type_index_core(m);
        bool unlocked = creation_type_unlocked_core(m);
        m.dx() = dx;
        m.si() = si;
        if (unlocked)
            break;
        auto value = m.rd8(m.ss(), m.si());
        if (forward) {
            value = uint8_t(value + 1);
            if (value > uint8_t(m.dx()))
                value = 0;
        } else
            value = value == 0 ? uint8_t(m.dx()) : uint8_t(value - 1);
        m.wr8(m.ss(), m.si(), value);
    }
    saved.restore(m);
}
// Swap the two selected 204-byte member histories and retain the final INC flags.
void swap_progress_blocks_core(Machine &m) {

    constexpr unsigned kQueueBytes = 204;
    const unsigned destination = kMemberQueues + uint8_t(m.bx()) * kQueueBytes;
    uint16_t source_address = uint16_t(kMemberQueues + m.al() * kQueueBytes),
             destination_address = uint16_t(destination);
    for (unsigned i = 0; i < kQueueBytes; ++i, source_address = uint16_t(source_address + 1),
                  destination_address = uint16_t(destination_address + 1)) {
        auto source_byte = m.rd8(m.ss(), source_address),
             destination_byte = m.rd8(m.ss(), destination_address);
        m.wr8(m.ss(), source_address, destination_byte);
        m.wr8(m.ss(), destination_address, source_byte);
    }
    // ROM 345FAC ends in INC DI, retaining CF from ADD DI,AX; LOOP and POPs preserve those flags.
    computed::flag(m, emu::CF, destination > 0xFFFFu);
    (void)computed::increment<uint16_t>(m, uint16_t(destination_address - 1));
}
void masked_character_tilemap_core(Machine &m) {
    const auto di = m.di();
    uint16_t dest = screen_map_offset(m.al(), m.bx());
    move_column(m, uint8_t(m.bx()));
    do {
        auto cx = m.cx();
        uint16_t row = dest;
        do {
            m.ax() = uint16_t((m.rd16(m.ds(), m.si()) & 0x1ff) + m.dx());
            m.wr16(m.ss(), row, m.ax());
            m.si() = uint16_t(m.si() + 2);
            row = uint16_t(row + 2);
            m.cx() = uint16_t((m.cx() & 0xff00) | uint8_t(m.cx() - 1));
        } while (uint8_t(m.cx()));
        m.cx() = cx;
        dest = uint16_t(dest + 64);
        m.cx() = uint16_t(m.cx() - 256);
    } while (m.cx() >> 8);
    m.di() = di;
}
// Load the selected creation type's large portrait and palette, then draw its tilemap.
void creation_large_graphic_core(Machine &m) {
    const MenuRegisters saved(m);
    m.ax() = table_at16(m, Table::monster_type_descriptor, 0x9000, uint16_t(0x7878 + m.al() * 2));
    m.bx() = m.ax();
    auto descriptor = m.bx();
    m.bp() = uint16_t(uint8_t(descriptor) * 6);
    m.ds() = m.es() = 0xe000;
    m.si() = m.rd16(m.es(), m.bp());
    m.cx() = m.rd16(m.es(), uint16_t(0xd8 + uint8_t(descriptor) * 2));
    m.ax() = 40;
    copy_tiles_core(m);
    m.si() = uint16_t(m.rd16(m.es(), uint16_t(m.bp() + 2)) + (descriptor >> 8) * 32);
    m.di() = kPaletteBuffer;
    m.ax() = 11;
    m.cx() = 1;
    copy_palettes_core(m);
    m.ax() = 1;
    m.bx() = 0x303;
    m.cx() = 0x606;
    m.dx() = 0x1ff;
    fill_screen_rect_core(m);
    m.cx() = m.rd16(m.es(), uint16_t(0x120 + uint8_t(descriptor) * 2));
    m.bx() = uint8_t(m.cx()) == 6 ? 0x303 : 0x304;
    m.ax() = 1;
    m.dx() = 0x1628;
    m.si() = m.rd16(m.es(), uint16_t(m.bp() + 4));
    masked_character_tilemap_core(m);
    saved.restore(m);
}
void creation_palette_source_core(Machine &m);
void creation_icon_position_core(Machine &m);
void creation_unlocked_icon_core(Machine &m, uint16_t table);
// Draw the selected creation template's walking icon in its current animation frame.
void creation_walking_icon_core(Machine &m) {
    const MenuRegisters saved(m);
    creation_type_index_core(m);
    const auto type = m.ax();
    creation_palette_source_core(m);
    m.ax() = 10;
    copy_palettes_core(m);
    creation_icon_position_core(m);
    m.ax() = type;
    m.dx() = m.rd8(m.ss(), kMenuThirdSelection);
    creation_unlocked_icon_core(m, 0x5e2a);
    saved.restore(m);
}
void template_stats_panel_core(Machine &m, bool full) {
    creation_type_index_core(m);
    m.di() = kSaveScratch;
    creation_template_core(m);
    m.si() = kSaveScratch;
    m.set_al(m.rd8(m.ss(), uint16_t(kSaveScratch + 5)));
    lookup_name_record_core(m);
    m.ax() = full ? 0 : 1;
    m.bx() = full ? 0xd02 : 0x902;
    draw_text_core(m);
    auto label_key = [&](TextKey key, uint16_t page, uint16_t position, uint16_t digits) {
        const TextPtr text_ptr = text(m, key);
        m.ds() = text_ptr.segment;
        m.si() = text_ptr.offset;
        m.ax() = page;
        m.bx() = position;
        m.cx() = digits;
        draw_text_n_white_core(m);
        text_done(m, key);
    };
    auto number = [&](unsigned stat_offset, uint16_t page, uint16_t position, uint16_t digits, bool byte) {
        m.si() = uint16_t(kSaveScratch + stat_offset);
        m.dx() = byte ? m.rd8(m.ss(), m.si()) : m.rd16(m.ss(), m.si());
        if (byte && m.dx() >= 99)
            m.dx() = 99;
        decimal_to_bcd_clamped_core(m);
        m.ax() = page;
        m.bx() = position;
        m.cx() = digits;
        draw_bcd_number_core(m);
    };
    // a language with value tokens (text_tokens.h) gets the numbers in its strings instead
    const TextKey hp_key =
        full ? TextKey::draw_creation_type_detail_screen_5 : TextKey::draw_creation_selected_preview_1;
    {
        TextValues values;
        values.set(U"hp", decimal_text(m.rd16(m.ss(), uint16_t(kSaveScratch + 6)), 3));
        label_key(hp_key, full ? 0 : 1, full ? 0xe03 : 0xa03, 2);
    }
    if (!has_value_tokens(m, hp_key))
        number(6, full ? 0 : 1, full ? 0xe08 : 0xa08, 3, false);
    constexpr TextKey detailLabels[] = {
        TextKey::draw_creation_type_detail_screen_6, TextKey::draw_creation_type_detail_screen_8,
        TextKey::draw_creation_type_detail_screen_10, TextKey::draw_creation_type_detail_screen_12};
    constexpr TextKey detailSeparators[] = {
        TextKey::draw_creation_type_detail_screen_7, TextKey::draw_creation_type_detail_screen_9,
        TextKey::draw_creation_type_detail_screen_11, TextKey::draw_creation_type_detail_screen_13};
    constexpr TextKey previewLabels[] = {
        TextKey::draw_creation_selected_preview_2, TextKey::draw_creation_selected_preview_3,
        TextKey::draw_creation_selected_preview_4, TextKey::draw_creation_selected_preview_5};
    for (unsigned i = 0; i < 4; ++i) {
        auto position = uint16_t(full ? 0x40d + i * 0x200 : 0xb03 + i * 0x100);
        label_key(full ? detailLabels[i] : previewLabels[i], 1, position, 4);
        if (full) {
            TextValues values;
            values.set(
                U"value",
                decimal_text(std::min<unsigned>(m.rd8(m.ss(), uint16_t(kSaveScratch + 11 + i)), 99), 2));
            label_key(detailSeparators[i], 1, uint16_t(position + 0x102), 1);
        }
        if (!full || !has_value_tokens(m, detailSeparators[i]))
            number(11 + i, 1, uint16_t(full ? position + 0x104 : position + 5), 2, true);
    }
    if (full) {
        m.ds() = 0;
        m.ax() = 1;
        m.cx() = 8;
        m.bx() = 0x414;
        m.si() = uint16_t(kSaveScratch + 15);
        draw_item_entry_rows_core(m);
    } else {
        menu_text(m, TextKey::draw_creation_selected_preview_6, 1, 0xf03);
        menu_text(m, TextKey::draw_creation_selected_preview_7, 1, 0x1003);
    }
}
// Render the selected creation template's portrait and stats without changing the live party.
void creation_selected_preview_core(Machine &m) {
    const MenuRegisters saved(m);
    m.ax() = 1;
    m.bx() = 0x302;
    m.cx() = 0xe08;
    m.dx() = 0x1ff;
    fill_screen_rect_core(m);
    m.ax() = 0;
    m.bx() = 0x302;
    m.cx() = 0xe08;
    m.dx() = 0x1fd9;
    fill_screen_rect_core(m);
    creation_type_index_core(m);
    creation_large_graphic_core(m);
    template_stats_panel_core(m, false);
    saved.restore(m);
}
void append_swap_label_sprites_core(Machine &m) {
    constexpr TextKey key = TextKey::append_party_swap_label_sprites_1;
    const TextPtr text_ptr = text(m, key);
    m.es() = text_ptr.segment;
    m.si() = text_ptr.offset;
    m.di() = uint16_t(kSpriteBuffer + m.in8(6) * 4);
    m.ax() = uint16_t(m.in8(6) * 4);
    m.cx() = 6;
    do {
        m.dx() = uint16_t(0x2f00 + m.rd8(m.es(), m.si()));
        m.wr16(m.ss(), m.di(), m.dx());
        m.wr16(m.ss(), uint16_t(m.di() + 2), m.bx());
        ++m.si();
        m.di() = uint16_t(m.di() + 4);
        move_row(m, 8);
    } while (--m.cx());
    text_done(m, key);
    m.set_al(uint8_t(m.in8(6) + 6));
    m.out8(6, m.al());
}
} // namespace
// rom: 0x3449AF (near)
// Draw equipment editor's inventory or key-item list and title.
void draw_equipment_inventory(Machine &m) {
    draw_equipment_inventory_core(m);
    m.ret();
}
// rom: 0x344A06 (near)
// Draw CX item rows from DS:SI at AX/BX, retaining final DX and SI/BH.
void draw_equipment_inventory_rows(Machine &m) {
    draw_item_entry_rows_core(m);
    m.ret();
}
// rom: 0x344A1E (near)
// Draw member equipment with four locked-slot labels when bit 5 is set.
void draw_member_equipment_rows(Machine &m) {
    equipment_rows_core(m);
    m.ret();
}
// Shared body of ROM 0x344A62
// Clear the left equipment marker area.
void clear_equipment_left_marker_core(Machine &m) {
    m.ax() = 1;
    m.bx() = 0x301;
    m.cx() = 0x202;
    m.dx() = 0x1ff;
    fill_screen_rect_core(m);
    return;
}

// rom: 0x344A62 (near)
void clear_equipment_left_marker(Machine &m) {
    clear_equipment_left_marker_core(m);
    m.ret();
}
// Shared body of ROM 0x344A74
// Clear the right equipment marker area.
void clear_equipment_right_marker_core(Machine &m) {
    m.ax() = 1;
    m.bx() = 0x31b;
    m.cx() = 0x202;
    m.dx() = 0x1ff;
    fill_screen_rect_core(m);
    return;
}

// rom: 0x344A74 (near)
void clear_equipment_right_marker(Machine &m) {
    clear_equipment_right_marker_core(m);
    m.ret();
}
// Shared body of ROM 0x344A86
// Draw the equipment inventory-list arrow, preserving DS.
void draw_equipment_inventory_arrow_core(Machine &m) {
    const auto ds = m.ds();
    m.ds() = 0x9000;
    m.si() = 0x3088;
    m.bx() = 0xa1b;
    m.cx() = 0x202;
    m.ax() = 1;
    draw_tilemap_core(m);
    m.ds() = ds;
    return;
}

// rom: 0x344A86 (near)
void draw_equipment_inventory_arrow(Machine &m) {
    draw_equipment_inventory_arrow_core(m);
    m.ret();
}
// Shared body of ROM 0x344A9F
// Clear the equipment inventory-list arrow area.
void clear_equipment_inventory_arrow_core(Machine &m) {
    m.ax() = 1;
    m.bx() = 0xa1b;
    m.cx() = 0x202;
    m.dx() = 0x1ff;
    fill_screen_rect_core(m);
    return;
}

// rom: 0x344A9F (near)
void clear_equipment_inventory_arrow(Machine &m) {
    clear_equipment_inventory_arrow_core(m);
    m.ret();
}
// Shared body of ROM 0x344CB1
// Validate party reorder, updating leader index D0E3 when swapped. SI/DI preserved; CF rejects.
void validate_party_reorder_core(Machine &m) {
    const auto saved_si = m.si(), saved_di = m.di();
    misc_cmp(m, saved_si, saved_di, 16);
    if (saved_si >= saved_di) {
        m.si() = saved_di;
        m.di() = saved_si;
    }
    bool reject = false;
    if (misc_cmp(m, m.rd8(m.ds(), m.si()), 255) || misc_cmp(m, m.rd8(m.ds(), m.di()), 255) ||
        misc_test(m, m.rd8(m.ds(), m.si()), 3) || misc_test(m, m.rd8(m.ds(), m.di()), 3))
        reject = true;
    else {
        m.set_al(m.rd8(m.ss(), kDialogueLeader));
        if (misc_cmp(m, m.al(), m.rd8(m.ss(), kMenuActionIndex)))
            m.set_al(m.rd8(m.ss(), kSaveSlotSelection));
        else if (misc_cmp(m, m.al(), m.rd8(m.ss(), kSaveSlotSelection)))
            m.set_al(m.rd8(m.ss(), kMenuActionIndex));
        else {
            m.si() = saved_si;
            m.di() = saved_di;
            misc_carry(m, false);
            return;
            return;
        }
        m.wr8(m.ss(), kDialogueLeader, m.al());
    }
    m.si() = saved_si;
    m.di() = saved_di;
    misc_carry(m, reject);
    return;
}

// rom: 0x344CB1 (near)
void validate_party_reorder(Machine &m) {
    validate_party_reorder_core(m);
    m.ret();
}
// Shared body of ROM 0x344CF9
// Swap two 31-byte party records then their 204-byte progress blocks.
void swap_party_records_core(Machine &m) {
    if (auto *session = Session::find(m.wsc());
        session && session->player_names().enabled && m.ds() == m.ss()) {
        const int source_member = (int(m.si()) - kGameState) / 31,
                  destination_member = (int(m.di()) - kGameState) / 31;
        if (source_member >= 0 && source_member < 4 && destination_member >= 0 && destination_member < 4 &&
            m.si() == kGameState + source_member * 31 && m.di() == kGameState + destination_member * 31)
            std::swap(session->player_names().values[size_t(source_member)],
                      session->player_names().values[size_t(destination_member)]);
    }
    m.cx() = 31;
    do {
        m.set_al(m.rd8(m.ds(), m.si()));
        m.set_ah(m.rd8(m.ds(), m.di()));
        m.wr8(m.ds(), m.si(), m.ah());
        m.wr8(m.ds(), m.di(), m.al());
        ++m.si();
        ++m.di();
    } while (--m.cx());
    m.set_al(m.rd8(m.ss(), kMenuActionIndex));
    m.bx() = uint16_t((m.bx() & 0xff00) | m.rd8(m.ds(), kSaveSlotSelection));
    swap_progress_blocks_core(m);
    return;
}

// rom: 0x344CF9 (near)
void swap_party_records(Machine &m) {
    swap_party_records_core(m);
    m.ret();
}
// Shared body of ROM 0x344E3D
// Every eighth animation step reload walking tiles for the party member being swapped; DS preserved.
void update_party_swap_tiles_core(Machine &m) {
    auto tick = uint8_t(m.rd8(m.ss(), kTabTransferCount + 2) + 1);
    m.wr8(m.ss(), kTabTransferCount + 2, tick);
    m.set_al(tick);
    if (!(tick & 7)) {
        const auto ds = m.ds();
        m.ds() = 0;
        m.ax() = uint16_t(m.rd8(m.ss(), kMenuActionIndex) * 31);
        m.dx() = uint16_t((m.dx() & 0xff00) | 31);
        m.si() = uint16_t(kGameState + m.ax());
        m.set_al(m.rd8(m.ss(), uint16_t(m.si() + 5)));
        char_type_to_gfx_index_core(m);
        m.bp() = uint16_t(m.al() * 4);
        m.es() = 0x9000;
        m.ds() = m.rd16(m.es(), uint16_t(0x3f70 + m.bp()));
        m.ax() = uint16_t(m.rd8(m.ss(), kMenuActionIndex) * 4);
        m.cx() = 4;
        m.si() = m.rd16(m.es(), uint16_t(0x40b0 + m.bp()));
        m.bx() = uint16_t(m.rd8(m.ss(), kTabTransferPage) + ((tick & 8) ? 1 : 0));
        m.bx() = uint16_t(m.bx() * 128);
        m.si() = uint16_t(m.si() + m.bx());
        copy_tiles_core(m);
        m.ds() = ds;
    }
    return;
}

// rom: 0x344E3D (near)
void update_party_swap_tiles(Machine &m) {
    update_party_swap_tiles_core(m);
    m.ret();
}
// Shared body of ROM 0x344EAA
// Draw swapped-member label window and append six label sprites.
void draw_party_swap_label_core(Machine &m) {
    m.ax() = uint16_t(m.rd8(m.ss(), kSaveSlotSelection) * 8);
    m.si() = uint16_t(0x4ed4 + m.ax());
    const auto saved_si = m.si();
    m.ax() = m.rd16(m.cs(), saved_si);
    m.bx() = m.rd16(m.cs(), uint16_t(saved_si + 2));
    m.cx() = 0x208;
    draw_window_core(m);
    m.si() = saved_si;
    auto label_position = m.rd16(m.cs(), uint16_t(saved_si + 4));
    m.bx() = uint16_t((label_position << 8) | (label_position >> 8));
    append_swap_label_sprites_core(m);
    return;
}

// rom: 0x344EAA (near)
void draw_party_swap_label(Machine &m) {
    draw_party_swap_label_core(m);
    m.ret();
}
// rom: 0x345101 (near)
// Append six vertical label sprites at BX, using current sprite count; ES=CS, count advances by six.
void append_party_swap_label_sprites(Machine &m) {
    append_swap_label_sprites_core(m);
    m.ret();
}
// Shared body of ROM 0x3454A2
// Search up to six forward creation types for an unlocked entry; registers preserved.
void select_next_creation_type_core(Machine &m) {
    advance_creation_type_core(m, true);
    return;
}

// rom: 0x3454A2 (near)
void select_next_creation_type(Machine &m) {
    select_next_creation_type_core(m);
    m.ret();
}
// Shared body of ROM 0x3454CA
// Search up to six backward creation types for an unlocked entry; registers preserved.
void select_previous_creation_type_core(Machine &m) {
    advance_creation_type_core(m, false);
    return;
}

// rom: 0x3454CA (near)
void select_previous_creation_type(Machine &m) {
    select_previous_creation_type_core(m);
    m.ret();
}
// rom: 0x345515 (near)
// Place the creation grid cursor using D843/D844 coordinates.
void draw_creation_grid_cursor_call_core(Machine &m) {
    m.ax() = uint16_t(m.rd8(m.cs(), uint16_t(0x553b + m.rd8(m.ss(), kSaveChoice))) |
                      (m.rd8(m.cs(), uint16_t(0x5541 + m.rd8(m.ss(), kMenuThirdSelection))) << 8));
    m.bx() = m.ax();
    m.ax() = 4;
    m.dx() = 0x3ffb;
    set_sprite_16x16_core(m);
}
void draw_creation_grid_cursor(Machine &m) {
    draw_creation_grid_cursor_call_core(m);
    m.ret();
}
// rom: 0x345546 (near)
// Place the creation equipment cursor, also writing its packed position at SS:014A (original quirk).
void draw_creation_equipment_cursor(Machine &m) {
    m.bx() = uint16_t(m.rd8(m.ss(), kMenuFourthSelection) * 2);
    m.ax() = m.rd16(m.cs(), uint16_t(0x5567 + m.bx()));
    m.bx() = m.ax();
    m.wr16(m.ss(), kMenuCursorPosition, m.bx());
    m.ax() = 4;
    m.dx() = 0x3ffb;
    set_sprite_16x16_core(m);
    m.ret();
}
// Shared body of ROM 0x345661
// Draw creation type screen, template stats, equipment and large graphic; all registers preserved.
void draw_creation_type_detail_screen_core(Machine &m) {
    const MenuRegisters saved(m);
    m.ds() = 0;
    for (unsigned i = 0; i < 4; ++i) {
        m.ax() = uint16_t(i * 4);
        m.bx() = 0xe0;
        m.dx() = 0x3ffb;
        set_sprite_16x16_core(m);
    }
    frame(m, 0, 1, 0x121c);
    m.ax() = 1;
    m.bx() = 1;
    m.cx() = 0x121c;
    m.dx() = 0x1ff;
    fill_screen_rect_core(m);
    constexpr uint16_t frames[][2] = {{1, 0x120a}, {0xc01, 0x40a},  {11, 0x1212},
                                      {11, 0x212}, {0x1001, 0x21c}, {1, 0x30a}};
    for (auto &f : frames)
        frame(m, 0, f[0], f[1]);
    menu_text(m, TextKey::draw_creation_type_detail_screen_1, 0, 0x102);
    menu_text(m, TextKey::draw_creation_type_detail_screen_2, 1, 14);
    menu_text(m, TextKey::draw_creation_type_detail_screen_3, 1, 0xe0c);
    m.ax() = uint16_t(m.rd8(m.ss(), kCreationLeader) * 5 + m.rd8(m.ss(), kMenuThirdSelection));
    m.bx() = 0x14;
    draw_creation_race_label_core(m);
    menu_text(m, TextKey::draw_creation_type_detail_screen_4, 0, 0x30c);
    template_stats_panel_core(m, true);
    creation_type_index_core(m);
    creation_large_graphic_core(m);
    saved.restore(m);
    return;
}

// rom: 0x345661 (near)
void draw_creation_type_detail_screen(Machine &m) {
    draw_creation_type_detail_screen_core(m);
    m.ret();
}
// Shared body of ROM 0x34591C
// Copy selected template item's 26-byte description to D80F and draw it. DS becomes SS.
void draw_creation_item_description_core(Machine &m) {
    m.ds() = m.ss();
    m.bx() = uint16_t(kSaveScratch + 15 + m.rd8(m.ds(), kMenuFourthSelection) * 2);
    m.di() = kItemDescriptionText;
    m.si() = 0x53e0;
    m.ax() = m.rd16(m.ds(), m.bx());
    const auto item = m.rd8(0, m.bx()), count = m.rd8(0, uint16_t(m.bx() + 1));
    m.ax() = uint16_t((count == 255 || item >= 252 ? 1 : 0) | ((count == 254 ? 1 : 0) << 8));
    m.ax() = m.al() ? 255 : m.rd8(m.ds(), m.bx());
    m.dx() = 27;
    auto product = uint32_t(m.ax()) * 27;
    m.ax() = uint16_t(product);
    m.dx() = uint16_t(product >> 16);
    m.si() = uint16_t(0x53e0 + m.ax());
    const auto ds = m.ds(), es = m.es();
    m.ds() = 0x9000;
    m.es() = 0;
    m.ax() = 0;
    m.cx() = 26;
    copy_bytes_terminated_core(m);
    m.ds() = ds;
    m.es() = es;
    m.ax() = 1;
    m.bx() = 0x1002;
    m.si() = kItemDescriptionText;
    draw_text_core(m);
    return;
}

// rom: 0x34591C (near)
void draw_creation_item_description(Machine &m) {
    draw_creation_item_description_core(m);
    m.ret();
}
// rom: 0x345970 (near)
// Draw selected creation type preview when unlocked, otherwise the locked-type message.
void draw_creation_unlock_preview_call_core(Machine &m) {
    creation_type_index_core(m);
    if (!creation_type_unlocked_core(m)) {
        m.ax() = 1;
        m.bx() = 0x302;
        m.cx() = 0xe08;
        m.dx() = 0x1ff;
        fill_screen_rect_core(m);
        m.ax() = 0;
        m.bx() = 0x302;
        m.cx() = 0xe08;
        m.dx() = 0x1fd9;
        fill_screen_rect_core(m);
        const auto ds = m.ds();
        menu_text(m, TextKey::draw_creation_unlock_preview_1, 0, 0xa02);
        menu_text(m, TextKey::draw_creation_unlock_preview_2, 1, 0x1003);
        m.ds() = ds;
    } else {
        creation_walking_icon_core(m);
        creation_selected_preview_core(m);
    }
}
void draw_creation_unlock_preview(Machine &m) {
    draw_creation_unlock_preview_call_core(m);
    m.ret();
}
// rom: 0x3459C3 (near)
// Draw unlocked creation type's large graphic and stats panel, preserving all registers.
void draw_creation_selected_preview(Machine &m) {
    creation_selected_preview_core(m);
    m.ret();
}

namespace {
void creation_palette_source_core(Machine &m) {
    char_type_to_gfx_index_core(m);
    auto graphic = m.ax();
    m.bp() = uint16_t(m.al() * 4);
    m.es() = 0x9000;
    m.ds() = m.rd16(m.es(), uint16_t(0x3f70 + m.bp()));
    m.ax() = uint16_t((graphic >> 8) * 32);
    m.si() = uint16_t(m.rd16(m.es(), uint16_t(0x40b2 + m.bp())) + m.ax());
    m.di() = kPaletteBuffer;
    m.ax() = uint16_t(m.rd8(m.ss(), kMenuThirdSelection) + 4);
    m.cx() = 1;
}
void creation_icon_position_core(Machine &m) {
    m.ax() = uint16_t(m.rd8(m.cs(), uint16_t(0x5c3f + m.rd8(m.ss(), kSaveChoice))) |
                      (m.rd8(m.cs(), uint16_t(0x5c46 + m.rd8(m.ss(), kMenuThirdSelection))) << 8));
    m.bx() = m.ax();
}
void creation_unlocked_icon_core(Machine &m, uint16_t table) {
    const auto saved_ax = m.ax(), saved_bx = m.bx(), saved_cx = m.cx(), saved_dx = m.dx(), ds = m.ds();
    if (creation_type_unlocked_core(m)) {
        m.ds() = m.cs();
        m.si() = uint16_t(table + uint16_t(m.dx() << 3));
        m.cx() = 0x202;
        m.ax() = 1;
        draw_tilemap_core(m);
    }
    m.ax() = saved_ax;
    m.bx() = saved_bx;
    m.cx() = saved_cx;
    m.dx() = saved_dx;
    m.ds() = ds;
}
void draw_option_panel_core(Machine &m, unsigned variant) {
    const MenuRegisters saved(m);
    m.ds() = 0;
    if (variant != 1) {
        m.ax() = 8;
        m.bx() = 0xe0;
        m.dx() = 0x3ffb;
        set_sprite_16x16_core(m);
    }
    frame(m, 0, 0x20c, 0xe11);
    constexpr uint16_t pos[][4] = {{0x610, 0, 0, 0},
                                   {0x610, 0x912, 0xa0e, 0xc10},
                                   {0x910, 0, 0, 0},
                                   {0x90d, 0x80d, 0x819, 0},
                                   {0x60e, 0x80e, 0xa0e, 0xc0e}};
    // Label keys per variant (unused tail entries are never read: see `counts`).
    constexpr TextKey keys[][4] = {
        {TextKey::draw_abbreviated_option_panel_1, TextKey::draw_abbreviated_option_panel_1,
         TextKey::draw_abbreviated_option_panel_1, TextKey::draw_abbreviated_option_panel_1},
        {TextKey::draw_full_option_panel_1, TextKey::draw_full_option_panel_2,
         TextKey::draw_full_option_panel_3, TextKey::draw_full_option_panel_4},
        {TextKey::draw_centered_option_panel_1, TextKey::draw_centered_option_panel_1,
         TextKey::draw_centered_option_panel_1, TextKey::draw_centered_option_panel_1},
        {TextKey::draw_dialogue_speed_panel_1, TextKey::draw_dialogue_speed_panel_2,
         TextKey::draw_dialogue_speed_panel_3, TextKey::draw_dialogue_speed_panel_3},
        {TextKey::draw_dialogue_party_order_panel_1, TextKey::draw_dialogue_party_order_panel_2,
         TextKey::draw_dialogue_party_order_panel_3, TextKey::draw_dialogue_party_order_panel_4}};
    constexpr unsigned counts[] = {1, 4, 1, 3, 4};
    for (unsigned i = 0; i < counts[variant]; ++i)
        menu_text(m, keys[variant][i], 0, pos[variant][i]);
    if (variant == 4)
        for (unsigned i = 0; i < 4; ++i) {
            m.dx() = uint16_t(m.rd8(m.ss(), uint16_t(kPartyOrder + i)) * 2);
            const TextKey key = party_menu_category(uint8_t(m.dx()) / 2);
            const TextPtr text_ptr = party_menu_category_text(m, uint8_t(m.dx() / 2));
            m.ds() = text_ptr.segment;
            m.si() = text_ptr.offset;
            m.ax() = 0;
            m.bx() = uint16_t(0x616 + i * 0x200);
            draw_text_core(m);
            text_done(m, key);
        }
    saved.restore(m);
}
void draw_dialogue_member_name_core(Machine &m) {
    const TextKey key = party_menu_category(uint8_t(m.dx()));
    const TextPtr text_ptr = party_menu_category_text(m, uint8_t(m.dx()));
    m.dx() = uint16_t(uint8_t(m.dx()) * 2);
    m.ds() = text_ptr.segment;
    m.si() = text_ptr.offset;
    draw_text_core(m);
    text_done(m, key);
}
void dialogue_cursor_core(Machine &m, uint16_t selection, uint16_t sprite) {
    m.bx() = uint16_t(m.rd8(m.ss(), selection) * 2);
    m.ax() = m.rd16(m.cs(), uint16_t(m.bx() + m.di()));
    m.bx() = m.ax();
    m.wr16(m.ss(), kMenuCursorPosition, m.bx());
    m.ax() = sprite;
    m.dx() = 0x3ffb;
    set_sprite_16x16_core(m);
}
} // namespace
// rom: 0x345B3C (near)
// Draw a creation type's alternate palette and unlocked walking icon, preserving all registers.
void draw_creation_type_alternate_icon_call_core(Machine &m) {
    const MenuRegisters saved(m);
    creation_type_index_core(m);
    auto type = m.ax();
    creation_palette_source_core(m);
    m.si() = uint16_t(m.rd16(m.es(), uint16_t(m.bp() + 0x40b2)) + 0xc0);
    copy_palettes_core(m);
    creation_icon_position_core(m);
    m.ax() = type;
    m.dx() = m.rd8(m.ss(), kMenuThirdSelection);
    creation_unlocked_icon_core(m, 0x5dd5);
    saved.restore(m);
}
void draw_creation_type_alternate_icon(Machine &m) {
    draw_creation_type_alternate_icon_call_core(m);
    m.ret();
}
// rom: 0x345B68 (near)
// Draw an unlocked creation type's normal walking icon and palette; all registers preserved.
void draw_creation_type_walking_icon(Machine &m) {
    creation_walking_icon_core(m);
    m.ret();
}
// rom: 0x345B8E (near)
// Load and draw AL's large character graphic in the preview window; all registers preserved.
void draw_creation_large_graphic(Machine &m) {
    creation_large_graphic_core(m);
    m.ret();
}
// rom: 0x345C24 (near)
// Return creation grid icon position in AX/BX using row/column byte tables.
void creation_icon_position(Machine &m) {
    creation_icon_position_core(m);
    m.ret();
}
// rom: 0x345C4B (near)
// Set graphics bank/palette source for character AL, with destination palette D844+4.
void creation_palette_source(Machine &m) {
    creation_palette_source_core(m);
    m.ret();
}
// rom: 0x345C82 (near)
// Compute creation type AX/BX = D852*30 + D844*6 + D843, retaining DH and DL=6.
void creation_type_index_call_core(Machine &m) { creation_type_index_core(m); }
void creation_type_index(Machine &m) {
    creation_type_index_call_core(m);
    m.ret();
}
// rom: 0x345E05 (near)
// Draw the unlocked type's walking icon selected by DX, preserving AX/BX/CX/DX/DS.
void draw_unlocked_creation_icon(Machine &m) {
    creation_unlocked_icon_core(m, 0x5e2a);
    m.ret();
}
// rom: 0x345F89
// Swap the AL/BL members' 204-byte progress blocks; all registers preserved.
void swap_member_progress_core(Machine &m) {
    swap_progress_blocks_core(m);
    return;
}

void swap_member_progress(Machine &m) {
    swap_member_progress_core(m);
    m.retf();
}
// rom: 0x345FC1
// Copy CHxCL character tiles with 9-bit tile masking plus DX attributes; DI preserved, BL doubled.
void draw_masked_character_tilemap(Machine &m) {
    masked_character_tilemap_core(m);
    m.retf();
}
// rom: 0x3465F3 (near)
// Load member DS:SI graphics into slot DX and draw at AX/BX coordinates; AX/BX preserved.
void draw_save_member_sprite(Machine &m) {
    const auto saved_ax = m.ax(), saved_bx = m.bx(), saved_dx = m.dx();
    m.bx() = m.si();
    load_member_graphics(m, m.bx(), m.dx(), true);
    m.ax() = saved_ax;
    m.bx() = saved_bx;
    m.dx() = saved_dx;
    m.ax() = uint16_t(saved_dx * 4);
    m.cx() = uint16_t(saved_dx << 9);
    m.dx() = uint16_t((saved_dx << 2) + 0x2000 + m.cx());
    m.bx() = uint16_t((uint8_t((saved_bx >> 8) * 8 + 4) << 8) | uint8_t(uint8_t(saved_bx + 1) * 8));
    set_sprite_16x16_core(m);
    m.ax() = saved_ax;
    m.bx() = saved_bx;
    m.ret();
}
// rom: 0x346627 (near)
// Load member SS:BX tiles and palette into menu graphics slot DX; DS/BX preserved.
void load_save_member_graphics(Machine &m) {
    load_member_graphics(m, m.bx(), m.dx(), true);
    m.ret();
}
// Shared body of ROM 0x346B18
// Store DS:D842 as the last SRAM slot; ES preserved, SI=3300, AH=10.
void write_last_save_slot_core(Machine &m) {
    m.ax() = 0x1000;
    m.set_al(m.rd8(m.ds(), kSaveSlotSelection));
    m.si() = 0x3300;
    if (SaveSlots *slots = save_store(m)) { // Save Slots 9: the selection is a row
        const int slot = slots->slot_for_row(m.al());
        slots->set_last(slot); // a slot the store holds is remembered there, never in 0x3300
        if (slots->holds(slot))
            return;
        m.set_al(uint8_t(slot));
    }
    m.wr8(0x1000, m.si(), m.al());
    return;
}

// rom: 0x346B18 (near)
void write_last_save_slot(Machine &m) {
    write_last_save_slot_core(m);
    m.ret();
}
// rom: 0x347223 (near)
// Position first options cursor from CS:DI and SS:D843; packed position also written to SS:014A.
void draw_options_primary_cursor_call_core(Machine &m) { dialogue_cursor_core(m, kSaveChoice, 4); }
void draw_options_primary_cursor(Machine &m) {
    draw_options_primary_cursor_call_core(m);
    m.ret();
}
// Shared body of ROM 0x347242
// Position second options cursor from CS:DI and SS:D844; packed position also written to SS:014A.
void draw_options_secondary_cursor_core(Machine &m) {
    dialogue_cursor_core(m, kMenuThirdSelection, 8);
    return;
}

// rom: 0x347242 (near)
void draw_options_secondary_cursor(Machine &m) {
    draw_options_secondary_cursor_core(m);
    m.ret();
}
// rom: 0x347401 (near)
// Draw the abbreviated option panel; omitted sprite/text calls in the ROM stay omitted.
void draw_abbreviated_option_panel_call_core(Machine &m) { draw_option_panel_core(m, 0); }
void draw_abbreviated_option_panel(Machine &m) {
    draw_abbreviated_option_panel_call_core(m);
    m.ret();
}
// rom: 0x34746C (near)
// Draw full option explanatory labels; all registers preserved.
void draw_full_option_panel_call_core(Machine &m) { draw_option_panel_core(m, 1); }
void draw_full_option_panel(Machine &m) {
    draw_full_option_panel_call_core(m);
    m.ret();
}
// rom: 0x3474E1 (near)
// Draw a centered option label and frame; all registers preserved.
void draw_centered_option_panel_call_core(Machine &m) { draw_option_panel_core(m, 2); }
void draw_centered_option_panel(Machine &m) {
    draw_centered_option_panel_call_core(m);
    m.ret();
}
// rom: 0x347581 (near)
// Draw dialogue-speed option labels; all registers preserved.
void draw_dialogue_speed_panel_call_core(Machine &m) { draw_option_panel_core(m, 3); }
void draw_dialogue_speed_panel(Machine &m) {
    draw_dialogue_speed_panel_call_core(m);
    m.ret();
}
// rom: 0x3475ED (near)
// Draw dialogue-party-order labels and current member names; all registers preserved.
void draw_dialogue_party_order_panel_call_core(Machine &m) { draw_option_panel_core(m, 4); }
void draw_dialogue_party_order_panel(Machine &m) {
    draw_dialogue_party_order_panel_call_core(m);
    m.ret();
}
// rom: 0x34769F (near)
// Draw DL's dialogue-party name at AX/BX using CS pointer table; DS=CS, DX doubled.
void draw_dialogue_member_name(Machine &m) {
    draw_dialogue_member_name_core(m);
    m.ret();
}
// Shared body of ROM 0x3476C7
// Draw the nine-name dialogue-party selection list; all registers preserved.
void draw_dialogue_member_list_core(Machine &m) {
    const MenuRegisters saved(m);
    m.ds() = 0;
    frame(m, 0, 0x215, 0xe08);
    m.ds() = m.cs();
    m.ax() = 0;
    m.bx() = 0x517;
    m.dx() = uint16_t((m.dx() & 0xff00) | 1);
    m.cx() = 9;
    do {
        const auto saved_ax = m.ax(), saved_bx = m.bx(), saved_cx = m.cx(), saved_dx = m.dx();
        draw_dialogue_member_name_core(m);
        m.ax() = saved_ax;
        m.bx() = uint16_t(saved_bx + 256);
        m.cx() = saved_cx;
        m.dx() = uint16_t((saved_dx & 0xff00) | uint8_t(saved_dx + 1));
    } while (--m.cx());
    saved.restore(m);
    return;
}

// rom: 0x3476C7 (near)
void draw_dialogue_member_list(Machine &m) {
    draw_dialogue_member_list_core(m);
    m.ret();
}
// rom: 0x3479B9 (near)
// Position load-menu cursor from CS:DI/SS:D842; packed position also written to SS:014A.
void draw_load_menu_cursor_call_core(Machine &m) { dialogue_cursor_core(m, kSaveSlotSelection, 24); }
void draw_load_menu_cursor(Machine &m) {
    draw_load_menu_cursor_call_core(m);
    m.ret();
}

namespace {
void draw_secondary_cursor_core(Machine &m, unsigned offset, bool hide, uint16_t sprite) {
    const MenuRegisters saved(m);
    m.ds() = 0;
    m.bx() = hide ? 0xe0 : m.rd16(0, kMenuCursorAnchor);
    if (!hide) {
        m.wr16(0, kSecondaryCursorPosition, m.bx());
        move_column(m, offset);
    }
    m.ax() = sprite;
    m.dx() = 0x3ffb;
    set_sprite_16x16_core(m);
    saved.restore(m);
}
void draw_member_race_abbreviation_core(Machine &m) {
    const auto saved_ax = m.ax(), saved_bx = m.bx(), saved_si = m.si(), ds = m.ds();
    unsigned bit = 0;
    auto race = m.rd8(m.ss(), saved_si);
    while (bit < 8 && !(race & 1)) {
        race >>= 1;
        ++bit;
    }
    m.si() = m.rd16(m.cs(), uint16_t(0x872c + bit * 2));
    m.cx() = 3;
    m.ds() = m.cs();
    draw_text_n_dark_core(m);
    m.ax() = saved_ax;
    m.bx() = saved_bx;
    m.si() = saved_si;
    m.ds() = ds;
}
void history_advance_core(Machine &m) {
    m.bp() = uint16_t(m.bp() + 1);
    if (m.bp() > 99)
        m.bp() = 0;
}
void draw_history_row_core(Machine &m) {
    m.set_al(m.rd8(m.ss(), uint16_t(m.bp() + m.si())));
    if (m.al() == 255)
        return;
    const auto saved_bx = m.bx(), saved_dx = m.dx(), saved_si = m.si();
    lookup_name_record_core(m);
    m.ax() = 1;
    draw_text_core(m);
    m.si() = saved_si;
    m.dx() = saved_dx;
    m.bx() = saved_bx;
    decimal_to_bcd_clamped_core(m);
    m.bx() = uint16_t(saved_bx + 14);
    m.ax() = 1;
    m.cx() = 4;
    draw_bcd_number_core(m);
}
void draw_history_page_core(Machine &m) {
    m.ax() = uint16_t(m.rd8(m.ss(), kMenuFifthSelection) * 2);
    m.si() = m.rd16(m.cs(), uint16_t(kProgressPointers + m.ax()));
    m.si() = uint16_t(m.si() + 2);
    m.bp() = m.rd16(m.ss(), m.si());
    history_advance_core(m);
    m.si() = uint16_t(m.si() + 2);
    m.cx() = m.rd16(m.ss(), kCreationRace);
    while (m.cx()) {
        history_advance_core(m);
        --m.cx();
    }
    m.dx() = uint16_t(m.rd16(m.ss(), kMenuSecondItemBuffer) - m.rd16(m.ss(), kCreationRace));
    m.cx() = 14;
    m.bx() = 0x20c;
    do {
        const auto saved_bx = m.bx(), saved_cx = m.cx(), saved_dx = m.dx(), saved_si = m.si();
        draw_history_row_core(m);
        m.bx() = saved_bx;
        m.cx() = saved_cx;
        m.dx() = saved_dx;
        m.si() = saved_si;
        history_advance_core(m);
        m.bx() = uint16_t(m.bx() + 256);
        --m.dx();
    } while (--m.cx());
    m.dx() = m.rd16(m.ss(), kCreationRace) == 0 ? 0x1ff : 0x1ff0;
    m.ax() = 1;
    m.bx() = 0x116;
    m.cx() = 0x101;
    fill_screen_rect_core(m);
    m.dx() = 0x1ff;
    m.bx() = uint16_t(m.rd16(m.ss(), kCreationRace) + 14);
    if (m.bx() < m.rd16(m.ss(), kNameInsertionIndex))
        m.dx() = 0x1ff1;
    m.ax() = 1;
    m.bx() = 0x1016;
    m.cx() = 0x101;
    fill_screen_rect_core(m);
}
void history_select_member_core(Machine &m, int step) {
    for (unsigned i = 0; i < 4; ++i) {
        auto index = m.rd8(m.ss(), m.si());
        auto member = uint16_t(kGameState + index * 31);
        if (m.rd8(m.ss(), member) != 255 && (m.rd8(m.ss(), uint16_t(member + 10)) & 64))
            break;
        m.wr8(m.ss(), m.si(), uint8_t(index + step) & 3);
    }
}
void load_compact_party_graphics_core(Machine &m) {
    m.ax() = 0;
    m.ds() = 0;
    constexpr uint16_t palettes[] = {8, 4, 5, 6};
    for (unsigned i = 0; i < 4; ++i) {
        auto member = uint16_t(kGameState + i * 31);
        m.set_al(m.rd8(0, uint16_t(member + 5)));
        char_type_to_gfx_index_core(m);
        auto gfx = m.ax();
        m.bp() = uint16_t(m.al() * 4);
        m.es() = 0x9000;
        m.ds() = m.rd16(m.es(), uint16_t(0x3f70 + m.bp()));
        m.ax() = uint16_t(0xd4 + i * 4);
        m.cx() = 4;
        m.si() = m.rd16(m.es(), uint16_t(0x40b0 + m.bp()));
        auto race = m.rd8(m.ss(), member);
        if (race & 1)
            m.si() = uint16_t(m.si() + 0x880);
        else if (race & 12)
            m.si() = uint16_t(m.si() + 0x800);
        copy_tiles_core(m);
        m.ax() = uint16_t((gfx >> 8) * 32);
        m.si() = uint16_t(m.rd16(m.es(), uint16_t(0x40b2 + m.bp())) + m.ax());
        m.di() = kPaletteBuffer;
        m.ax() = palettes[i];
        m.cx() = 1;
        if (race & 2)
            m.si() = uint16_t(m.rd16(m.es(), uint16_t(0x40b2 + m.bp())) + 0xc0);
        copy_palettes_core(m);
        m.ds() = 0;
    }
}
void draw_compact_member_icon_core(Machine &m) {
    if (m.rd8(m.ss(), uint16_t(kGameState + uint8_t(m.dx()) * 31)) == 255)
        return;
    const auto saved_ax = m.ax(), saved_bx = m.bx(), saved_cx = m.cx(), saved_dx = m.dx(), ds = m.ds();
    m.ds() = m.cs();
    m.si() = uint16_t(0x926f + uint16_t(m.dx() << 3));
    m.cx() = 0x202;
    draw_tilemap_core(m);
    m.ax() = saved_ax;
    m.bx() = saved_bx;
    m.cx() = saved_cx;
    m.dx() = saved_dx;
    m.ds() = ds;
}
void draw_history_member_core(Machine &m) {
    const auto saved_ax = m.ax(), saved_bx = m.bx(), saved_dx = m.dx();
    m.si() = uint16_t(kGameState + uint8_t(saved_dx) * 31);
    if (m.rd8(m.ss(), m.si()) == 255)
        return;
    const auto saved_si = m.si();
    m.ax() = 1;
    draw_compact_member_icon_core(m);
    m.ax() = saved_ax;
    m.bx() = saved_bx;
    m.si() = saved_si;
    const auto ds = m.ds();
    m.ds() = m.ss();
    m.ax() = 0;
    m.bx() = uint16_t(saved_bx + 0x102);
    ++m.si();
    m.cx() = 4;
    draw_text_n_white_core(m);
    m.ax() = saved_ax;
    m.bx() = saved_bx;
    m.si() = saved_si;
    m.ds() = ds;
}
void draw_history_members_core(Machine &m) {
    const MenuRegisters saved(m);
    load_compact_party_graphics_core(m);
    for (unsigned i = 0; i < 4; ++i) {
        m.ax() = 1;
        m.bx() = uint16_t(0x303 + i * 0x200);
        m.dx() = uint16_t(i);
        draw_history_member_core(m);
    }
    saved.restore(m);
}
void draw_inn_member_summary_core(Machine &m) {
    if (m.rd8(m.ss(), m.si()) == 255)
        return;
    const auto saved_ax = m.ax(), saved_bx = m.bx(), saved_si = m.si(), ds = m.ds();
    auto restore = [&]() {
        m.ax() = saved_ax;
        m.bx() = saved_bx;
        m.si() = saved_si;
        m.ds() = ds;
    };
    ++m.si();
    m.cx() = 4;
    draw_text_n_white_core(m);
    restore();
    move_column(m, 4);
    m.cx() = 2;
    // a language with value tokens (text_tokens.h) gets the numbers in its strings instead
    const bool heart_tokens = has_value_tokens(m, TextKey::draw_inn_member_summary_1);
    const bool hp_tokens = has_value_tokens(m, TextKey::draw_inn_member_summary_2);
    {
        TextValues values;
        values.set(U"hearts", member_hearts(m, m.ds(), saved_si));
        const TextPtr text_ptr = text(m, TextKey::draw_inn_member_summary_1);
        m.ds() = text_ptr.segment;
        m.si() = text_ptr.offset;
        draw_text_n_white_core(m);
        text_done(m, TextKey::draw_inn_member_summary_1);
    }
    restore();
    if (!heart_tokens) {
        m.si() = uint16_t(saved_si + 10);
        m.dx() = m.rd8(m.ds(), m.si()) & 15;
        decimal_to_bcd_clamped_core(m);
        m.ax() = saved_ax;
        m.bx() = saved_bx;
        move_column(m, 6);
        draw_bcd_left_core(m);
        restore();
    }
    move_row(m, 1);
    m.cx() = 7;
    {
        TextValues values;
        values.set(U"hp", member_hp(m, m.ds(), saved_si));
        values.set(U"maxhp", member_maxhp(m, m.ds(), saved_si));
        const TextPtr text_ptr = text(m, TextKey::draw_inn_member_summary_2);
        m.ds() = text_ptr.segment;
        m.si() = text_ptr.offset;
        draw_text_n_white_core(m);
        text_done(m, TextKey::draw_inn_member_summary_2);
    }
    restore();
    if (hp_tokens)
        return;
    m.si() = uint16_t(saved_si + 6);
    m.dx() = m.rd16(m.ds(), m.si());
    decimal_to_bcd_clamped_core(m);
    m.ax() = saved_ax;
    m.bx() = saved_bx;
    move_row(m, 1);
    move_column(m, 2);
    m.cx() = 3;
    draw_bcd_number_core(m);
    restore();
    move_row(m, 1);
    if (m.rd8(m.ds(), saved_si) == 0) {
        m.si() = uint16_t(saved_si + 8);
        m.dx() = m.rd16(m.ds(), m.si());
        decimal_to_bcd_clamped_core(m);
        m.ax() = saved_ax;
        m.bx() = saved_bx;
        move_row(m, 1);
        move_column(m, 6);
        m.cx() = 3;
        draw_bcd_number_core(m);
    } else {
        move_column(m, 4);
        draw_member_race_abbreviation_core(m);
    }
    restore();
}
void draw_inn_party_core(Machine &m) {
    const MenuRegisters saved(m);
    load_compact_party_graphics_core(m);
    for (unsigned i = 0; i < 4; ++i) {
        m.ax() = 1;
        m.bx() = uint16_t(0x402 + i * 0x200);
        m.dx() = uint16_t(i);
        draw_compact_member_icon_core(m);
    }
    m.ds() = 0;
    for (unsigned i = 0; i < 4; ++i) {
        m.ax() = 1;
        m.bx() = uint16_t(0x404 + i * 0x200);
        m.si() = uint16_t(kGameState + i * 31);
        draw_inn_member_summary_core(m);
    }
    saved.restore(m);
}
// Choose and copy the current item/command description into the shared text buffer, then draw it.
void menu_description_core(Machine &m) {
    const MenuRegisters saved(m);
    m.ax() = 0;
    m.ds() = 0;
    m.es() = 0x9000;
    auto mode = m.rd8(0, kMenuDescriptionMode);
    if (mode == 1 || mode == 2 || mode == 3) {
        auto slot = m.rd8(m.ss(), kCreationRace);
        auto member = uint16_t(kGameState + slot * 31);
        auto inventory = m.rd8(0, kCreationLeader) != 0;
        auto selection = m.rd8(0, inventory ? kSaveChoice : kSaveSlotSelection);
        uint16_t base =
            mode == 1
                ? (inventory ? kKeyItems : kInventory)
                : (inventory ? (mode == 3 && m.rd8(0, kNameInsertionIndex) != 0 ? kKeyItems : kInventory)
                             : uint16_t(member + 15));
        m.bx() = uint16_t(base + selection * 2);
        m.di() = kItemDescriptionText;
        uint16_t source_segment = 0x9000;
        bool special = false;
        TextKey key{};
        const bool disabled = mode == 3 && (m.rd8(m.ss(), uint16_t(member + 10)) & 64);
        if (!disabled && !inventory && selection >= 8 && selection <= 9) {
            // D_34B9C6 / D_34B9E1: the Crystal and Trash rows' descriptions
            key = (mode == 1 ? selection == 8 : selection == 9) ? TextKey::draw_inventory_item_description_1
                                                                : TextKey::draw_inventory_item_description_2;
            const TextPtr text_ptr = text(m, key);
            m.si() = text_ptr.offset;
            source_segment = text_ptr.segment;
            special = true;
        }
        if (!special) {
            auto item = m.rd8(0, m.bx()), count = m.rd8(0, uint16_t(m.bx() + 1));
            if (count == 255 || item >= 252 || (mode == 3 && (m.rd8(m.ss(), uint16_t(member + 10)) & 64)))
                item = 255;
            m.si() = uint16_t(0x53e0 + item * 27);
        }
        auto ds = m.ds(), es = m.es();
        m.ds() = source_segment;
        m.es() = 0;
        m.ax() = 0;
        m.cx() = 26;
        copy_bytes_terminated_core(m);
        if (special)
            text_done(m, key);
        m.ds() = mode == 3 ? m.ss() : ds;
        m.es() = es;
        m.ax() = 1;
        m.bx() = 0x1002;
        m.si() = kItemDescriptionText;
        draw_text_core(m);
    } else if (mode == 7) {
        auto choice = m.rd8(0, kSaveSlotSelection);
        // D_34BE80 (blank), then the Config help lines D_34BB0F/BB2A/BB45/BB60 (field_menu_label_0..3)
        constexpr TextKey texts[] = {TextKey::draw_inventory_command_description_1,
                                     TextKey::field_menu_label_0, TextKey::field_menu_label_1,
                                     TextKey::field_menu_label_2, TextKey::field_menu_label_3};
        menu_text(m, choice < 5 ? texts[choice] : texts[0], 1, 0x1002);
    }
    saved.restore(m);
}
} // namespace
// rom: 0x3479F7 (near)
// Draw load-menu windows, labels and three save-slot summaries; all registers preserved.
void draw_load_menu_screen_call_core(Machine &m) {
    const MenuRegisters saved(m);
    m.ds() = 0;
    for (unsigned i = 0; i < 8; ++i) {
        m.ax() = uint16_t(i * 4);
        m.bx() = 0xe0;
        m.dx() = 0x3ffb;
        set_sprite_16x16_core(m);
    }
    frame(m, 0, 1, 0x121c);
    m.ax() = 1;
    m.bx() = 1;
    m.cx() = 0x21c;
    m.dx() = 0x1ff;
    fill_screen_rect_core(m);
    constexpr uint16_t frames[][2] = {{1, 0xd1c}, {1, 0x81c}, {1, 0x307}, {8, 0x315}};
    for (auto &f : frames)
        frame(m, 0, f[0], f[1]);
    menu_text(m, TextKey::draw_load_menu_screen_1, 0, 0x103);
    menu_text(m, TextKey::draw_load_menu_screen_2, 1, 9);
    menu_text(m, TextKey::draw_load_menu_screen_3, 1, 0x110);
    draw_save_slot_summaries_core(m);
    saved.restore(m);
}
void draw_load_menu_screen(Machine &m) {
    draw_load_menu_screen_call_core(m);
    m.ret();
}
// rom: 0x347D3F (near)
// Draw up to CX dark glyphs (1E00 attributes) at AX/BX, stopping at FF; DI/ES preserved, DF clear.
void draw_text_n_dark(Machine &m) {
    draw_text_n_dark_core(m);
    m.ret();
}
// Shared body of ROM 0x3481E2
// Save the primary cursor and draw a second cursor four pixels to its right; registers preserved.
void show_secondary_menu_cursor_core(Machine &m) {
    draw_secondary_cursor_core(m, 4, false, 4);
    return;
}

// rom: 0x3481E2 (near)
void show_secondary_menu_cursor(Machine &m) {
    show_secondary_menu_cursor_core(m);
    m.ret();
}
// Shared body of ROM 0x348206
// Save the primary cursor and draw a second cursor six pixels to its right; registers preserved.
void show_refill_menu_cursor_core(Machine &m) {
    draw_secondary_cursor_core(m, 6, false, 4);
    return;
}

// rom: 0x348206 (near)
void show_refill_menu_cursor(Machine &m) {
    show_refill_menu_cursor_core(m);
    m.ret();
}
// Shared body of ROM 0x34822A
// Hide the secondary menu cursor, preserving all registers.
void hide_secondary_menu_cursor_core(Machine &m) {
    draw_secondary_cursor_core(m, 0, true, 4);
    return;
}

// rom: 0x34822A (near)
void hide_secondary_menu_cursor(Machine &m) {
    hide_secondary_menu_cursor_core(m);
    m.ret();
}
// Shared body of ROM 0x348246
// Hide the primary menu cursor, preserving all registers.
void hide_primary_menu_cursor_core(Machine &m) {
    draw_secondary_cursor_core(m, 0, true, 0);
    return;
}

// rom: 0x348246 (near)
void hide_primary_menu_cursor(Machine &m) {
    hide_primary_menu_cursor_core(m);
    m.ret();
}
// Shared body of ROM 0x348262
// Dispatch the selected menu's item/option description; all registers preserved, including no-op cases.
void draw_menu_selection_description_core(Machine &m) {
    menu_description_core(m);
    return;
}

// rom: 0x348262 (near)
void draw_menu_selection_description(Machine &m) {
    draw_menu_selection_description_core(m);
    m.ret();
}
// Shared body of ROM 0x3484FD
// Return AL-indexed packed progress nibble at D0A6 in AX; even indices use high nibble, BX preserved.
void read_menu_progress_nibble_core(Machine &m) {
    auto counter_index = m.al(),
         packed_counters = m.rd8(m.ss(), uint16_t(kPackedProgress + (counter_index >> 1)));
    m.ax() = (counter_index & 1) ? packed_counters & 15 : packed_counters >> 4;
    return;
}

// rom: 0x3484FD (near)
void read_menu_progress_nibble(Machine &m) {
    read_menu_progress_nibble_core(m);
    m.ret();
}
// rom: 0x3486FC (near)
// Draw the member's first-set-bit race abbreviation, preserving AX/BX/SI/DS; CX is remaining glyph count.
void draw_member_race_abbreviation(Machine &m) {
    draw_member_race_abbreviation_core(m);
    m.ret();
}
// rom: 0x34895D (near)
// Draw history-member cursor from D846, writing packed position to SS:014A.
void draw_history_member_cursor(Machine &m) {
    auto di = m.di();
    m.di() = 0x897e;
    dialogue_cursor_core(m, kMenuFifthSelection, 0);
    m.di() = di;
    m.ret();
}
// rom: 0x348986 (near)
// Draw history-row cursor from D847, writing packed position to SS:014A.
void draw_history_row_cursor(Machine &m) {
    auto di = m.di();
    m.di() = 0x89a7;
    dialogue_cursor_core(m, kMenuSixthSelection, 4);
    m.di() = di;
    m.ret();
}
// rom: 0x3489C5 (near)
// Draw monster history screen labels and four member selectors; all registers preserved.
void draw_monster_history_screen_call_core(Machine &m) {
    const MenuRegisters saved(m);
    for (unsigned i = 0; i < 4; ++i) {
        m.ax() = uint16_t(i * 4);
        m.bx() = 0xe0;
        m.dx() = 0x3ffb;
        set_sprite_16x16_core(m);
    }
    m.ax() = 1;
    m.bx() = 1;
    m.cx() = 0x121c;
    m.dx() = 0x1ff;
    fill_screen_rect_core(m);
    frame(m, 0, 0xe01, 0x40a);
    // with {generation}/{name} (text_tokens.h) the selected member's draw has the labels, at the values'
    // cells
    if (!has_value_tokens(m, TextKey::draw_monster_history_screen_1))
        menu_text(m, TextKey::draw_monster_history_screen_1, 0, 0x1007);
    if (!has_value_tokens(m, TextKey::draw_monster_history_screen_2))
        menu_text(m, TextKey::draw_monster_history_screen_2, 1, 0x12);
    menu_text(m, TextKey::draw_monster_history_screen_3, 1, 0xb03);
    menu_text(m, TextKey::draw_monster_history_screen_4, 1, 0xc03);
    draw_history_members_core(m);
    saved.restore(m);
}
void draw_monster_history_screen(Machine &m) {
    draw_monster_history_screen_call_core(m);
    m.ret();
}
// rom: 0x348A6D (near)
// Load compact party graphics and draw four history-member names/icons; all registers preserved.
void draw_history_members(Machine &m) {
    draw_history_members_core(m);
    m.ret();
}
// rom: 0x348AA9 (near)
// Draw member DL's history selector name/icon, preserving AX/BX/DX and DS around text.
void draw_history_member(Machine &m) {
    draw_history_member_core(m);
    m.ret();
}
// rom: 0x348AE6 (near)
// Draw selected monster's name/history count, count occupied history entries, and render its page.
void draw_selected_monster_history_call_core(Machine &m) {
    m.ax() = 1;
    m.bx() = 0x10b;
    m.cx() = 0x1212;
    m.dx() = 0x1ff;
    fill_screen_rect_core(m);
    m.ax() = uint16_t(m.rd8(m.ss(), kMenuFifthSelection) * 31);
    m.dx() = uint16_t((m.dx() & 0xff00) | 31);
    auto member = uint16_t(kGameState + m.ax());
    m.si() = member;
    m.set_al(m.rd8(m.ss(), uint16_t(member + 5)));
    lookup_name_record_core(m);
    m.ax() = 0;
    m.bx() = 0xf02;
    draw_text_core(m);
    m.ds() = m.ss();
    if (has_value_tokens(m, TextKey::draw_monster_history_screen_2)) {
        const auto ds = m.ds();
        TextValues values;
        std::u32string name =
            Session::of(m.wsc()).player_names().rendered(m.rd8(m.ss(), kMenuFifthSelection));
        values.set(U"name", name.empty() ? ram_text(m, m.ss(), uint16_t(member + 1), 4) : name);
        menu_text(m, TextKey::draw_monster_history_screen_2, 1, 13);
        m.ds() = ds;
    } else {
        m.si() = uint16_t(member + 1);
        m.ax() = 1;
        m.bx() = 13;
        m.cx() = 4;
        draw_text_n_white_core(m);
    }
    m.ax() = uint16_t(m.rd8(m.ss(), kMenuFifthSelection) * 2);
    m.si() = m.rd16(m.cs(), uint16_t(kProgressPointers + m.ax()));
    m.dx() = m.rd16(m.ss(), m.si());
    m.wr16(m.ss(), kMenuSecondItemBuffer, m.dx());
    if (has_value_tokens(m, TextKey::draw_monster_history_screen_1)) {
        const auto ds = m.ds();
        TextValues values;
        values.set(U"generation", decimal_text(m.dx(), 4));
        menu_text(m, TextKey::draw_monster_history_screen_1, 0, 0x1002); // the number's first cell
        m.ds() = ds;
    } else {
        decimal_to_bcd_clamped_core(m);
        m.ax() = 0;
        m.bx() = 0x1005;
        m.cx() = 4;
        draw_bcd_number_core(m);
    }
    m.ax() = uint16_t(m.rd8(m.ss(), kMenuFifthSelection) * 2);
    m.si() = uint16_t(m.rd16(m.cs(), uint16_t(kProgressPointers + m.ax())) + 4);
    m.cx() = 100;
    m.ax() = 0;
    do {
        if (m.rd8(m.ss(), m.si()) != 255)
            ++m.ax();
        ++m.si();
    } while (--m.cx());
    m.wr16(m.ss(), kNameInsertionIndex, m.ax());
    draw_history_page_core(m);
}
void draw_selected_monster_history(Machine &m) {
    draw_selected_monster_history_call_core(m);
    m.ret();
}
// Shared body of ROM 0x348B83
// Render fourteen history entries and scroll arrows from the selected member's ring buffer.
void draw_monster_history_page_core(Machine &m) {
    draw_history_page_core(m);
    return;
}

// rom: 0x348B83 (near)
void draw_monster_history_page(Machine &m) {
    draw_monster_history_page_core(m);
    m.ret();
}
// rom: 0x348C12 (near)
// Draw monster at SS:BP+SI and history counter DX; FF entries leave text untouched.
void draw_monster_history_row(Machine &m) {
    draw_history_row_core(m);
    m.ret();
}
// rom: 0x348CE1 (near)
// Advance the history ring index BP, wrapping every value above 99 to zero.
void advance_monster_history_index(Machine &m) {
    history_advance_core(m);
    m.ret();
}
// rom: 0x348CF3 (near)
// Advance SS:SI party selection to a member with equipment bit 6, probing at most four slots.
void select_next_history_member_call_core(Machine &m) { history_select_member_core(m, 1); }
void select_next_history_member(Machine &m) {
    select_next_history_member_call_core(m);
    m.ret();
}
// rom: 0x348D25 (near)
// Retreat SS:SI party selection to a member with equipment bit 6, probing at most four slots.
void select_previous_history_member_call_core(Machine &m) { history_select_member_core(m, -1); }
void select_previous_history_member(Machine &m) {
    select_previous_history_member_call_core(m);
    m.ret();
}
// rom: 0x348F0B (near)
// Draw inn confirmation cursor from D841's two-entry position table.
void draw_inn_confirmation_cursor_call_core(Machine &m) {
    m.bx() = uint16_t(m.rd8(m.ss(), kMenuSelection) * 2);
    m.ax() = m.rd16(m.cs(), uint16_t(0x8f27 + m.bx()));
    m.bx() = m.ax();
    m.ax() = 0;
    m.dx() = 0x3ffb;
    set_sprite_16x16_core(m);
}
void draw_inn_confirmation_cursor(Machine &m) {
    draw_inn_confirmation_cursor_call_core(m);
    m.ret();
}
// rom: 0x348F2B (near)
// Price the stay at one Gil per missing HP among living members and draw the inn confirmation.
void draw_inn_screen_call_core(Machine &m) {
    const MenuRegisters saved(m);
    frame(m, 1, 1, 0x307);
    menu_text(m, TextKey::draw_inn_screen_1, 1, 0x103);
    frame(m, 0, 0x301, 0xa0b);
    frame(m, 1, 0x301, 0xa0b);
    draw_inn_party_core(m);
    frame(m, 1, 0xd01, 0x51c);
    menu_text(m, TextKey::draw_inn_screen_2, 1, 0xe05);
    menu_text(m, TextKey::draw_inn_screen_3, 1, 0xf05);
    frame(m, 1, 0x617, 0x406);
    menu_text(m, TextKey::draw_inn_screen_4, 1, 0x718);
    menu_text(m, TextKey::draw_inn_screen_5, 1, 0x818);
    frame(m, 1, 0xa13, 0x30a);
    format_gold_core(m);
    gold_text(m, TextKey::draw_inn_screen_6, 1, 0xb14);
    m.ax() = 0;
    m.bx() = 0;
    m.si() = kGameState;
    m.cx() = 4;
    do {
        if (!(m.rd8(m.ss(), m.si()) & 1)) {
            m.ax() = uint16_t(m.ax() + m.rd16(m.ss(), uint16_t(m.si() + 6)));
            m.bx() = uint16_t(m.bx() + m.rd16(m.ss(), uint16_t(m.si() + 8)));
        }
        m.si() = uint16_t(m.si() + 31);
    } while (--m.cx());
    // Inn price = sum(max HP) - sum(current HP) for members without the death bit; no charge for ability
    // refills.
    m.bx() = uint16_t(m.bx() - m.ax());
    m.ax() = m.bx();
    m.dx() = 0;
    m.wr16(m.ss(), kMenuPriceLow, m.ax());
    m.wr16(m.ss(), kMenuPriceHigh, 0);
    format_amount_core(m, kMenuPriceLow, kMenuPriceHigh);
    gold_text(m, TextKey::draw_inn_screen_7, 1, 0xf14);
    saved.restore(m);
}
void draw_inn_screen(Machine &m) {
    draw_inn_screen_call_core(m);
    m.ret();
}
// rom: 0x34905C (near)
// Draw compact party icons and name/HP summaries for the inn; all registers preserved.
void draw_inn_party(Machine &m) {
    draw_inn_party_core(m);
    m.ret();
}

// rom: 0x349195 (near)
// Restore saved menu sprite/palette/screens and sprite count; only AL changes, flags preserved.
void restore_saved_menu_buffers_call_core(Machine &m) {
    restore_menu_dma(m, kSpriteBackup, kSpriteBuffer, 0x200);
    restore_menu_dma(m, kPaletteTarget, kPaletteBuffer, 0x200);
    m.set_al(m.rd8(m.ss(), kMenuDescriptionSpriteStart));
    m.out8(6, m.al());
    restore_menu_dma(m, kOverlayScreen, kScreenMap, 0x800);
    restore_menu_dma(m, kMenuScreenBackup, kScreenMap2, 0x800);
}
void restore_saved_menu_buffers(Machine &m) {
    restore_saved_menu_buffers_call_core(m);
    m.ret();
}
// rom: 0x3491F4 (near)
// Format the shared 32-bit service price for inn, revive and shop displays.
void format_inn_cost_call_core(Machine &m) { format_amount_core(m, kMenuPriceLow, kMenuPriceHigh); }
void format_inn_cost(Machine &m) {
    format_inn_cost_call_core(m);
    m.ret();
}
// rom: 0x34923C (near)
// Draw member DL's compact 2x2 icon when present; AX/BX/CX/DX/DS preserved, SI points at tiles.
void draw_compact_member_icon_call_core(Machine &m) { draw_compact_member_icon_core(m); }
void draw_compact_member_icon(Machine &m) {
    draw_compact_member_icon_call_core(m);
    m.ret();
}
// rom: 0x34928F (near)
// Draw one inn party member's name, equipment count and health, preserving AX/BX/SI/DS.
void draw_inn_member_summary_call_core(Machine &m) { draw_inn_member_summary_core(m); }
void draw_inn_member_summary(Machine &m) {
    draw_inn_member_summary_call_core(m);
    m.ret();
}
// rom: 0x34935B (near)
// Load four compact party graphics at tiles D4/D8/DC/E0 and palettes 8/4/5/6; DS ends zero.
void load_compact_party_graphics_call_core(Machine &m) { load_compact_party_graphics_core(m); }
void load_compact_party_graphics(Machine &m) {
    load_compact_party_graphics_call_core(m);
    m.ret();
}
// Shared body of ROM 0x34959A
// Clear three inn message rows with the blank text string, preserving DS.
void clear_inn_message_core(Machine &m) {
    const auto ds = m.ds();
    constexpr TextKey keys[] = {TextKey::clear_inn_message_1, TextKey::clear_inn_message_2,
                                TextKey::clear_inn_message_3};
    for (unsigned i = 0; i < 3; ++i)
        menu_text(m, keys[i], 1, uint16_t(0xe02 + i * 0x100));
    m.ds() = ds;
    return;
}

// rom: 0x34959A (near)
void clear_inn_message(Machine &m) {
    clear_inn_message_core(m);
    m.ret();
}

} // namespace sawgod::game

namespace sawgod::game {
namespace {

[[maybe_unused]] constexpr uint16_t kVignetteBackdropColor = 0xFFE0;
} // namespace
// Scanline wait at ROM 340051 is a timing boundary, like wait_vblank: a native routine (bank34.cpp's
// wait_visible_scanline), reached like any other far call-out.
void wait_visible_scanline_core(Machine &m) { m.call_far(0x4000, 0x0051); }
// rom: 0x340696
// Decrease each RGB nibble that differs from the target in AX's CX palettes.
void step_selected_palettes_down_to_target_core(Machine &m) {
    computed::save_registers(m);
    m.cx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.cx() & 0x1F)));
    if (m.flags() & emu::ZF)
        goto branch_06ed;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() & 0xF)));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x5));
    m.si() = uint16_t(kPaletteTarget);
    m.di() = uint16_t(kPaletteBuffer);
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), m.ax()));
    m.cx() = uint16_t(computed::shift_left<uint16_t>(m, m.cx(), 0x4));
branch_06b0:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.di())));
    m.dx() = uint16_t(m.rd16(m.ss(), uint16_t(m.si())));
    m.bx() = uint16_t(m.ax());
    m.cx() = uint16_t(m.dx());
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0xF))));
    m.dx() =
        uint16_t((m.dx() & 0xFF00) | uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.dx()) & 0xF))));
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.dx()));
    if (m.flags() & emu::ZF)
        goto branch_06c6;
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.bx()), true)));
branch_06c6:;
    m.set_al(uint8_t(uint8_t(m.bx())));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0xF0))));
    m.cx() =
        uint16_t((m.cx() & 0xFF00) | uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.cx()) & 0xF0))));
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.cx()));
    if (m.flags() & emu::ZF)
        goto branch_06d4;
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0x10)));
branch_06d4:;
    m.bx() =
        uint16_t((m.bx() & 0x00FF) |
                 (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx() >> 8) & 0xF)))) << 8));
    m.cx() =
        uint16_t((m.cx() & 0x00FF) |
                 (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.cx() >> 8) & 0xF)))) << 8));
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx() >> 8), uint8_t(m.cx() >> 8));
    if (m.flags() & emu::ZF)
        goto branch_06e0;
    m.bx() = uint16_t((m.bx() & 0x00FF) |
                      (uint16_t(uint8_t(computed::increment<uint8_t>(m, uint8_t(m.bx() >> 8), true))) << 8));
branch_06e0:;
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.bx()));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x2));
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_06b0;
branch_06ed:;
    computed::restore_registers(m);
    return;
}
void step_selected_palettes_down_to_target(Machine &m) {
    step_selected_palettes_down_to_target_core(m);
    if (m.cs() == 0x4000)
        m.retf();
}
// rom: 0x340815
// Place a 16x16 sprite at the current sprite cursor and advance it by four slots.
void append_sprite_16x16_core(Machine &m) {
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kOamCount))));
    set_sprite_16x16_core(m);
    m.set_al(uint8_t(0x4));
    m.wr8(m.ss(), uint16_t(kOamCount),
          uint8_t(computed::add<uint8_t>(m, m.rd8(m.ss(), uint16_t(kOamCount)), m.al())));
    return;
}
void append_sprite_16x16(Machine &m) {
    append_sprite_16x16_core(m);
    if (m.cs() == 0x4000)
        m.retf();
}
// rom: 0x340828
// Place an 8x8 sprite at the current sprite cursor and advance it by one slot.
void append_sprite_8x8_core(Machine &m) {
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kOamCount))));
    set_sprite_16x16_core(m);
    m.wr8(m.ss(), uint16_t(kOamCount),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kOamCount)))));
    return;
}
void append_sprite_8x8(Machine &m) {
    append_sprite_8x8_core(m);
    if (m.cs() == 0x4000)
        m.retf();
}
// rom: 0x340892
// Copy a CL by CH grid of tile words at DS:SI into sprites starting at AX and BL:BH.
void draw_sprite_tile_grid_core(Machine &m) {
    computed::push(m, m.si());
    computed::push(m, m.di());
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    {
        const uint16_t exchanged = uint8_t(m.bx());
        m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(uint8_t(m.bx() >> 8)));
        m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(exchanged)) << 8));
    }
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x2));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), kSpriteBuffer));
    m.di() = uint16_t(m.ax());
branch_08a0:;
    computed::push(m, m.bx());
    computed::push(m, m.cx());
branch_08a2:;
    m.ax() = uint16_t(m.rd16(m.ds(), uint16_t(m.si())));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(m.di() + 0x0002), uint16_t(m.bx()));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.bx() = uint16_t((m.bx() & 0x00FF) |
                      (uint16_t(uint8_t(computed::add<uint8_t>(m, uint8_t(m.bx() >> 8), 0x8))) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    if (!(m.flags() & emu::ZF))
        goto branch_08a2;
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::add<uint8_t>(m, uint8_t(m.bx()), 0x8)));
    m.cx() = uint16_t((m.cx() & 0x00FF) |
                      (uint16_t(uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx() >> 8), true))) << 8));
    if (!(m.flags() & emu::ZF))
        goto branch_08a0;
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    m.di() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::pop(m));
    return;
}
void draw_sprite_tile_grid(Machine &m) {
    draw_sprite_tile_grid_core(m);
    if (m.cs() == 0x4000)
        m.retf();
}
// rom: 0x3408C6
// Add DX to each tile word while drawing a CL by CH sprite grid.
void draw_sprite_tile_grid_offset_core(Machine &m) {
    computed::push(m, m.si());
    computed::push(m, m.di());
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    {
        const uint16_t exchanged = uint8_t(m.bx());
        m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(uint8_t(m.bx() >> 8)));
        m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(exchanged)) << 8));
    }
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x2));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), kSpriteBuffer));
    m.di() = uint16_t(m.ax());
branch_08d4:;
    computed::push(m, m.bx());
    computed::push(m, m.cx());
branch_08d6:;
    m.ax() = uint16_t(m.rd16(m.ds(), uint16_t(m.si())));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.dx()));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(m.di() + 0x0002), uint16_t(m.bx()));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.bx() = uint16_t((m.bx() & 0x00FF) |
                      (uint16_t(uint8_t(computed::add<uint8_t>(m, uint8_t(m.bx() >> 8), 0x8))) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    if (!(m.flags() & emu::ZF))
        goto branch_08d6;
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::add<uint8_t>(m, uint8_t(m.bx()), 0x8)));
    m.cx() = uint16_t((m.cx() & 0x00FF) |
                      (uint16_t(uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx() >> 8), true))) << 8));
    if (!(m.flags() & emu::ZF))
        goto branch_08d4;
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    m.di() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::pop(m));
    return;
}
void draw_sprite_tile_grid_offset(Machine &m) {
    draw_sprite_tile_grid_offset_core(m);
    if (m.cs() == 0x4000)
        m.retf();
}
// rom: 0x3409B0
// Convert AL row and BL/BH column/screen into the screen-map address in DI.
void screen_cell_pointer_core(Machine &m) {
    computed::push(m, m.ax());
    computed::push(m, m.bx());
    m.set_ah(uint8_t(m.al()));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() ^ m.al()))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x3));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), kScreenMap));
    computed::push(m, m.bx());
    m.bx() = uint16_t((m.bx() & 0xFF00) |
                      uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx()) ^ uint8_t(m.bx())))));
    m.bx() = uint16_t(computed::shift_right<uint16_t>(m, m.bx(), 0x2));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.bx()));
    m.bx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::shift_left<uint8_t>(m, uint8_t(m.bx()), 0x1)));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), uint8_t(m.bx()))));
    m.di() = uint16_t(m.ax());
    m.bx() = uint16_t(computed::pop(m));
    m.ax() = uint16_t(computed::pop(m));
    return;
}
void screen_cell_pointer(Machine &m) {
    screen_cell_pointer_core(m);
    if (m.cs() == 0x4000)
        m.retf();
}
// rom: 0x3409CE
// Draw a string with dialogue-speed delays until A skips the remaining delays.
void draw_text_with_key_skip_core(Machine &m) {
    m.watch_text("draw_text_with_key_skip");
    computed::push(m, m.es());
    computed::push(m, m.di());
    computed::push(m, m.cx());
    m.flags() &= ~emu::DF;
    computed::push(m, m.bx());
    m.set_ah(uint8_t(m.al()));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() ^ m.al()))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x3));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), kScreenMap));
    computed::push(m, m.bx());
    m.bx() = uint16_t((m.bx() & 0xFF00) |
                      uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx()) ^ uint8_t(m.bx())))));
    m.bx() = uint16_t(computed::shift_right<uint16_t>(m, m.bx(), 0x2));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.bx()));
    m.bx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::shift_left<uint8_t>(m, uint8_t(m.bx()), 0x1)));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), uint8_t(m.bx()))));
    m.di() = uint16_t(m.ax());
    m.bx() = uint16_t(computed::pop(m));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr8(m.ss(), uint16_t(kDialogueSkipDelay), uint8_t(m.al()));
    m.es() = uint16_t(m.ax());
    if (const std::u32string *text = proportional_text(m)) { // a loaded language's string: our font, at once
        const int cells = draw_table_text(m, m.di(), 0x1F00, *text);
        skip_text(m);
        m.ax() = 0x1FFF;
        m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(uint8_t(m.bx()) + cells));
        goto branch_0a39;
    }
branch_09f5:;
    m.ax() = uint16_t(0x1F00);
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si()))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_0a39;
    m.si() = uint16_t(computed::increment<uint16_t>(m, m.si()));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.bx()))));
    m.wr16(m.es(), uint16_t(m.di()), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kDialogueSkipDelay))));
    m.cx() = uint16_t((m.cx() & 0xFF00) |
                      uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.cx()) | uint8_t(m.cx())))));
    if (!(m.flags() & emu::ZF))
        goto branch_0a37;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kDialogueSpeed))));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()))));
branch_0a17:;
    m.call_far(0x4000, 0x0009);
    m.call_far(0x4000, 0x0009);
    m.call_far(0x4000, 0x005C);
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x4)));
    if (!(m.flags() & emu::ZF))
        goto branch_0a31;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    if (!(m.flags() & emu::ZF))
        goto branch_0a17;
    goto branch_0a37;
branch_0a31:;
    m.wr8(m.ss(), uint16_t(kDialogueSkipDelay), uint8_t(0x1));
branch_0a37:;
    goto branch_09f5;
branch_0a39:;
    m.cx() = uint16_t(computed::pop(m));
    m.di() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    return;
}
void draw_text_with_key_skip(Machine &m) {
    draw_text_with_key_skip_core(m);
    if (m.cs() == 0x4000)
        m.retf();
}
// rom: 0x340BC2
// Add 64 to AL before selecting the music request.
void play_alternate_music_core(Machine &m) {
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x40)));
    m.ax() = m.rd16(m.cs(), uint16_t(0xcd7 + m.al() * 2));
    return;
}
void play_alternate_music(Machine &m) {
    play_alternate_music_core(m);
    if (m.cs() == 0x4000)
        m.retf();
}
// rom: 0x341ECB (near)
// Copy a CL by CH rectangle between RAM screen maps at SI and DI.
void copy_screen_rectangle_core(Machine &m) {
    computed::push(m, m.ds());
    computed::push(m, m.di());
    m.ax() = uint16_t(m.bx());
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() ^ m.al()))));
    m.ax() = uint16_t(computed::shift_right<uint16_t>(m, m.ax(), 0x2));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::shift_left<uint8_t>(m, uint8_t(m.bx()), 0x1)));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), uint8_t(m.bx()))));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), m.ax()));
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x40);
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), uint8_t(m.cx()))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), uint8_t(m.cx()))));
branch_1ee8:;
    computed::push(m, m.cx());
branch_1ee9:;
    m.dx() = uint16_t(m.rd16(m.ds(), uint16_t(m.si())));
    m.wr16(m.ds(), uint16_t(m.di()), uint16_t(m.dx()));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    if (!(m.flags() & emu::ZF))
        goto branch_1ee9;
    m.cx() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), m.ax()));
    m.cx() = uint16_t((m.cx() & 0x00FF) |
                      (uint16_t(uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx() >> 8), true))) << 8));
    if (!(m.flags() & emu::ZF))
        goto branch_1ee8;
    m.di() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    return;
}
void copy_screen_rectangle(Machine &m) {
    copy_screen_rectangle_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x341F03 (near)
// Copy the scroll-selected 32 by 32 tile view into ES with wraparound.
void copy_wrapped_screen_view_core(Machine &m) {
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x8)));
    m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() ^ m.bx())));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() & 0xF0F0)));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.ah()));
    m.bx() = uint16_t(computed::shift_left<uint16_t>(m, m.bx(), 0x3));
    m.set_al(uint8_t(computed::shift_right<uint8_t>(m, m.al(), 0x2)));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::add<uint8_t>(m, uint8_t(m.bx()), m.al())));
    m.di() = uint16_t(0x0);
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x10)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x10));
branch_1f1b:;
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    computed::push(m, m.di());
branch_1f1e:;
    m.ax() = uint16_t(m.rd16(m.ds(), uint16_t(m.bx())));
    m.wr16(m.es(), uint16_t(m.di()), uint16_t(m.ax()));
    m.ax() = uint16_t(m.rd16(m.ds(), uint16_t(m.bx() + 0x0002)));
    m.wr16(m.es(), uint16_t(m.di() + 0x0002), uint16_t(m.ax()));
    m.ax() = uint16_t(m.rd16(m.ds(), uint16_t(m.bx() + 0x0040)));
    m.wr16(m.es(), uint16_t(m.di() + 0x0040), uint16_t(m.ax()));
    m.ax() = uint16_t(m.rd16(m.ds(), uint16_t(m.bx() + 0x0042)));
    m.wr16(m.es(), uint16_t(m.di() + 0x0042), uint16_t(m.ax()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), 0x4));
    m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() & 0xFFBF)));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    if (!(m.flags() & emu::ZF))
        goto branch_1f1e;
    m.di() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), 0x80));
    m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() & 0xF7FF)));
    m.cx() = uint16_t((m.cx() & 0x00FF) |
                      (uint16_t(uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx() >> 8), true))) << 8));
    if (!(m.flags() & emu::ZF))
        goto branch_1f1b;
    return;
}
void copy_wrapped_screen_view(Machine &m) {
    copy_wrapped_screen_view_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x34527B (near)
// Browse character types and toggle their event-flag unlocks.
void run_creation_unlock_browser_core(Machine &m) {
    UiSceneScope bestiary_scope(m, hooks::Event::Bestiary, 5); // presentation only
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.set_al(uint8_t(0x0));
branch_5282:;
    hooks::emit(hooks::Event::Bestiary, m, {5}); // grid restored after detail
    draw_creation_grid_cursor_call_core(m);
    draw_creation_unlock_preview_call_core(m);
    dma_sprite_table_core(m);
    m.call_far(0x4000, 0x0009);
    dma_palettes_core(m);
branch_5297:;
    m.call_far(0x4000, 0x005C);
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kKeysRepeat)));
    m.dx() = uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(0x5)) << 8));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x4));
    m.si() = uint16_t(kSaveChoice);
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x8));
    if (!(m.flags() & emu::ZF))
        goto branch_52ec;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x4));
    if (!(m.flags() & emu::ZF))
        goto branch_52f5;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x10));
    if (!(m.flags() & emu::ZF))
        goto branch_5330;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x40));
    if (m.flags() & emu::ZF)
        goto branch_52be;
    goto branch_5344;
branch_52be:;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x80));
    if (!(m.flags() & emu::ZF))
        goto branch_5312;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x20));
    if (!(m.flags() & emu::ZF))
        goto branch_5320;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x800));
    if (m.flags() & emu::ZF)
        goto branch_52d0;
    goto branch_5369;
branch_52d0:;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x200));
    if (m.flags() & emu::ZF)
        goto branch_52d8;
    goto branch_5379;
branch_52d8:;
    draw_creation_grid_cursor_call_core(m);
    dma_sprite_table_core(m);
    dma_palettes_core(m);
    m.call_far(0x4000, 0x0009);
    goto branch_5297;
branch_52ec:;
    m.ax() = uint16_t(0x94);
    trigger_sound_effect_core(m);
    goto branch_53d2;
branch_52f5:;
    creation_type_index_call_core(m);
    test_event_flag_call_core(m);
    if (!(m.flags() & emu::ZF))
        goto branch_5300;
    goto branch_53cf;
branch_5300:;
    m.ax() = uint16_t(0x93);
    trigger_sound_effect_core(m);
    run_growth_review_menu_core(m);
    draw_race_selection_screen_core(m);
    draw_class_review_cursor_core(m);
    goto branch_5282;
branch_5312:;
    draw_creation_type_alternate_icon_call_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x1)));
    if (!(m.flags() & emu::CF))
        goto branch_535a;
    m.set_al(uint8_t(uint8_t(m.dx() >> 8)));
    goto branch_535a;
branch_5320:;
    draw_creation_type_alternate_icon_call_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.dx() >> 8));
    if (m.flags() & (emu::CF | emu::ZF))
        goto branch_535a;
    m.set_al(uint8_t(0x0));
    goto branch_535a;
branch_5330:;
    draw_creation_type_alternate_icon_call_core(m);
    m.si() = uint16_t(computed::increment<uint16_t>(m, m.si()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x1)));
    if (!(m.flags() & emu::CF))
        goto branch_535a;
    m.set_al(uint8_t(uint8_t(m.dx())));
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    goto branch_5369;
branch_5344:;
    draw_creation_type_alternate_icon_call_core(m);
    m.si() = uint16_t(computed::increment<uint16_t>(m, m.si()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.dx()));
    if (m.flags() & (emu::CF | emu::ZF))
        goto branch_535a;
    m.set_al(uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    goto branch_5379;
branch_535a:;
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    draw_creation_unlock_preview_call_core(m);
    m.ax() = uint16_t(0x92);
    trigger_sound_effect_core(m);
    goto branch_52d8;
branch_5369:;
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x4));
    m.si() = uint16_t(kCreationLeader);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x1)));
    if (!(m.flags() & emu::CF))
        goto branch_5389;
    m.set_al(uint8_t(uint8_t(m.dx())));
    goto branch_5389;
branch_5379:;
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x4));
    m.si() = uint16_t(kCreationLeader);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.dx()));
    if (m.flags() & (emu::CF | emu::ZF))
        goto branch_5389;
    m.set_al(uint8_t(0x0));
branch_5389:;
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    draw_creation_races_entry_core(m);
    draw_creation_unlock_preview_call_core(m);
    m.ax() = uint16_t(0x92);
    trigger_sound_effect_core(m);
    goto branch_52d8;
branch_53cf:;
    goto branch_52d8;
branch_53d2:;
    return;
}
void run_creation_unlock_browser(Machine &m) {
    run_creation_unlock_browser_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x346C1A (near)
// Select the dash setting using option bits 0 and 2.
void run_dash_setting_menu_core(Machine &m) {
    m.ax() = uint16_t(0x93);
    trigger_sound_effect_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kOptions))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x1))));
    m.wr8(m.ss(), uint16_t(kSaveChoice), uint8_t(m.al()));
branch_6c2a:;
    m.di() = uint16_t(0x6CC3);
    draw_options_primary_cursor_call_core(m);
    draw_field_menu_label_core(m);
    draw_abbreviated_option_panel_call_core(m);
    dma_sprite_table_core(m);
    m.call_far(0x4000, 0x0009);
    dma_palettes_core(m);
branch_6c45:;
    m.call_far(0x4000, 0x005C);
    m.ax() = uint16_t(m.rd16(m.ds(), uint16_t(kKeysRepeat)));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x1));
    m.si() = uint16_t(kSaveChoice);
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x8));
    if (!(m.flags() & emu::ZF))
        goto branch_6c7d;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x4));
    if (!(m.flags() & emu::ZF))
        goto branch_6c85;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x80));
    if (!(m.flags() & emu::ZF))
        goto branch_6ca1;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x20));
    if (!(m.flags() & emu::ZF))
        goto branch_6cac;
branch_6c66:;
    m.di() = uint16_t(0x6CC3);
    draw_options_primary_cursor_call_core(m);
    dma_sprite_table_core(m);
    dma_palettes_core(m);
    m.call_far(0x4000, 0x0009);
    goto branch_6c45;
branch_6c7d:;
    m.ax() = uint16_t(0x94);
    trigger_sound_effect_core(m);
    goto branch_6cc2;
branch_6c85:;
    m.ax() = uint16_t(0x93);
    trigger_sound_effect_core(m);
    m.bx() = uint16_t(
        (m.bx() & 0x00FF) |
        (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx() >> 8) ^ uint8_t(m.bx() >> 8)))))
         << 8));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.bx() = uint16_t(computed::shift_left<uint16_t>(m, m.bx(), 0x1));
    m.ax() = uint16_t(m.rd16(m.cs(), uint16_t(m.bx() + 0x6C9D)));
    {
        const uint16_t callback_cs = m.cs();
        dispatch_computed_handler_core(m, m.ax());
        if (m.cs() != callback_cs)
            return;
    }
    if (m.flags() & emu::CF)
        goto branch_6cc2;
    goto branch_6c2a;
branch_6ca1:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x1)));
    if (!(m.flags() & emu::CF))
        goto branch_6cb7;
    m.set_al(uint8_t(uint8_t(m.dx())));
    goto branch_6cb7;
branch_6cac:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.dx()));
    if (m.flags() & (emu::CF | emu::ZF))
        goto branch_6cb7;
    m.set_al(uint8_t(0x0));
branch_6cb7:;
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    m.ax() = uint16_t(0x92);
    trigger_sound_effect_core(m);
    goto branch_6c66;
branch_6cc2:;
    return;
}
void run_dash_setting_menu(Machine &m) {
    run_dash_setting_menu_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x346CC7 (near)
// Clear battle option bit 0 and return with carry set.
void disable_dash_core(Machine &m) {
    m.wr8(m.ss(), uint16_t(kOptions),
          uint8_t(computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ss(), uint16_t(kOptions)) & 0xFE))));
    m.flags() |= emu::CF;
    return;
}
void disable_dash(Machine &m) {
    disable_dash_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x346CCF (near)
// Select whether holding or releasing B activates dash.
void run_dash_button_mode_menu_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kOptions))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x4))));
    m.set_al(uint8_t(computed::shift_right<uint8_t>(m, m.al(), 0x2)));
    m.wr8(m.ss(), uint16_t(kMenuThirdSelection), uint8_t(m.al()));
branch_6cdc:;
    m.di() = uint16_t(0x6D75);
    draw_options_secondary_cursor_core(m);
    draw_field_menu_label_core(m);
    draw_full_option_panel_call_core(m);
    dma_sprite_table_core(m);
    m.call_far(0x4000, 0x0009);
    dma_palettes_core(m);
branch_6cf7:;
    m.call_far(0x4000, 0x005C);
    m.ax() = uint16_t(m.rd16(m.ds(), uint16_t(kKeysRepeat)));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x1));
    m.si() = uint16_t(kMenuThirdSelection);
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x8));
    if (!(m.flags() & emu::ZF))
        goto branch_6d2f;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x4));
    if (!(m.flags() & emu::ZF))
        goto branch_6d37;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x80));
    if (!(m.flags() & emu::ZF))
        goto branch_6d53;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x20));
    if (!(m.flags() & emu::ZF))
        goto branch_6d5e;
branch_6d18:;
    m.di() = uint16_t(0x6D75);
    draw_options_secondary_cursor_core(m);
    dma_sprite_table_core(m);
    dma_palettes_core(m);
    m.call_far(0x4000, 0x0009);
    goto branch_6cf7;
branch_6d2f:;
    m.ax() = uint16_t(0x94);
    trigger_sound_effect_core(m);
    goto branch_6d74;
branch_6d37:;
    m.ax() = uint16_t(0x93);
    trigger_sound_effect_core(m);
    m.bx() = uint16_t(
        (m.bx() & 0x00FF) |
        (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx() >> 8) ^ uint8_t(m.bx() >> 8)))))
         << 8));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.bx() = uint16_t(computed::shift_left<uint16_t>(m, m.bx(), 0x1));
    m.ax() = uint16_t(m.rd16(m.cs(), uint16_t(m.bx() + 0x6D4F)));
    {
        const uint16_t callback_cs = m.cs();
        dispatch_computed_handler_core(m, m.ax());
        if (m.cs() != callback_cs)
            return;
    }
    if (m.flags() & emu::CF)
        goto branch_6d74;
    goto branch_6cdc;
branch_6d53:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x1)));
    if (!(m.flags() & emu::CF))
        goto branch_6d69;
    m.set_al(uint8_t(uint8_t(m.dx())));
    goto branch_6d69;
branch_6d5e:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.dx()));
    if (m.flags() & (emu::CF | emu::ZF))
        goto branch_6d69;
    m.set_al(uint8_t(0x0));
branch_6d69:;
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    m.ax() = uint16_t(0x92);
    trigger_sound_effect_core(m);
    goto branch_6d18;
branch_6d74:;
    return;
}
void run_dash_button_mode_menu(Machine &m) {
    run_dash_button_mode_menu_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x346D79 (near)
// Set battle option bit 0 and clear bit 2.
void dash_while_button_held_core(Machine &m) {
    m.wr8(m.ss(), uint16_t(kOptions),
          uint8_t(computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ss(), uint16_t(kOptions)) & 0xFE))));
    m.wr8(m.ss(), uint16_t(kOptions),
          uint8_t(computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ss(), uint16_t(kOptions)) | 0x1))));
    m.wr8(m.ss(), uint16_t(kOptions),
          uint8_t(computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ss(), uint16_t(kOptions)) & 0xFB))));
    m.flags() |= emu::CF;
    return;
}
void dash_while_button_held(Machine &m) {
    dash_while_button_held_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x346D8D (near)
// Set battle option bits 0 and 2.
void dash_while_button_released_core(Machine &m) {
    m.wr8(m.ss(), uint16_t(kOptions),
          uint8_t(computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ss(), uint16_t(kOptions)) & 0xFE))));
    m.wr8(m.ss(), uint16_t(kOptions),
          uint8_t(computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ss(), uint16_t(kOptions)) | 0x1))));
    m.wr8(m.ss(), uint16_t(kOptions),
          uint8_t(computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ss(), uint16_t(kOptions)) & 0xFB))));
    m.wr8(m.ss(), uint16_t(kOptions),
          uint8_t(computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ss(), uint16_t(kOptions)) | 0x4))));
    m.flags() |= emu::CF;
    return;
}
void dash_while_button_released(Machine &m) {
    dash_while_button_released_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x346DA7 (near)
// Select automatic retargeting when an action loses its target.
void run_auto_target_setting_menu_core(Machine &m) {
    hooks::emit(hooks::Event::BattleAutoTargetSetting, m, {1});
    m.ax() = uint16_t(0x93);
    trigger_sound_effect_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kOptions))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x2))));
    m.set_al(uint8_t(computed::shift_right<uint8_t>(m, m.al(), 0x1)));
    m.wr8(m.ss(), uint16_t(kSaveChoice), uint8_t(m.al()));
branch_6db9:;
    m.di() = uint16_t(0x6E52);
    draw_options_primary_cursor_call_core(m);
    draw_field_menu_label_core(m);
    draw_centered_option_panel_call_core(m);
    dma_sprite_table_core(m);
    m.call_far(0x4000, 0x0009);
    dma_palettes_core(m);
branch_6dd4:;
    m.call_far(0x4000, 0x005C);
    m.ax() = uint16_t(m.rd16(m.ds(), uint16_t(kKeysRepeat)));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x1));
    m.si() = uint16_t(kSaveChoice);
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x8));
    if (!(m.flags() & emu::ZF))
        goto branch_6e0c;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x4));
    if (!(m.flags() & emu::ZF))
        goto branch_6e14;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x80));
    if (!(m.flags() & emu::ZF))
        goto branch_6e30;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x20));
    if (!(m.flags() & emu::ZF))
        goto branch_6e3b;
branch_6df5:;
    m.di() = uint16_t(0x6E52);
    draw_options_primary_cursor_call_core(m);
    dma_sprite_table_core(m);
    dma_palettes_core(m);
    m.call_far(0x4000, 0x0009);
    goto branch_6dd4;
branch_6e0c:;
    m.ax() = uint16_t(0x94);
    trigger_sound_effect_core(m);
    goto branch_6e51;
branch_6e14:;
    m.ax() = uint16_t(0x93);
    trigger_sound_effect_core(m);
    m.bx() = uint16_t(
        (m.bx() & 0x00FF) |
        (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx() >> 8) ^ uint8_t(m.bx() >> 8)))))
         << 8));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.bx() = uint16_t(computed::shift_left<uint16_t>(m, m.bx(), 0x1));
    m.ax() = uint16_t(m.rd16(m.cs(), uint16_t(m.bx() + 0x6E2C)));
    {
        const uint16_t callback_cs = m.cs();
        dispatch_computed_handler_core(m, m.ax());
        if (m.cs() != callback_cs)
            return;
    }
    if (m.flags() & emu::CF)
        goto branch_6e51;
    goto branch_6db9;
branch_6e30:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x1)));
    if (!(m.flags() & emu::CF))
        goto branch_6e46;
    m.set_al(uint8_t(uint8_t(m.dx())));
    goto branch_6e46;
branch_6e3b:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.dx()));
    if (m.flags() & (emu::CF | emu::ZF))
        goto branch_6e46;
    m.set_al(uint8_t(0x0));
branch_6e46:;
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    m.ax() = uint16_t(0x92);
    trigger_sound_effect_core(m);
    goto branch_6df5;
branch_6e51:;
    hooks::emit(hooks::Event::BattleAutoTargetSetting, m, {0});
    return;
}
void run_auto_target_setting_menu(Machine &m) {
    run_auto_target_setting_menu_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x346E56 (near)
// Clear battle option bit 1.
void disable_auto_target_core(Machine &m) {
    m.wr8(m.ss(), uint16_t(kOptions),
          uint8_t(computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ss(), uint16_t(kOptions)) & 0xFD))));
    m.flags() |= emu::CF;
    return;
}
void disable_auto_target(Machine &m) {
    disable_auto_target_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x346E5E (near)
// Set battle option bit 1.
void enable_auto_target_core(Machine &m) {
    m.wr8(m.ss(), uint16_t(kOptions),
          uint8_t(computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ss(), uint16_t(kOptions)) & 0xFD))));
    m.wr8(m.ss(), uint16_t(kOptions),
          uint8_t(computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ss(), uint16_t(kOptions)) | 0x2))));
    m.flags() |= emu::CF;
    return;
}
void enable_auto_target(Machine &m) {
    enable_auto_target_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x346E6C (near)
// Select whether to preview transformations before eating meat.
void run_monster_preview_setting_menu_core(Machine &m) {
    m.ax() = uint16_t(0x93);
    trigger_sound_effect_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kOptions))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x8))));
    m.set_al(uint8_t(computed::shift_right<uint8_t>(m, m.al(), 0x3)));
    m.wr8(m.ss(), uint16_t(kSaveChoice), uint8_t(m.al()));
branch_6e7f:;
    m.di() = uint16_t(0x6F18);
    draw_options_primary_cursor_call_core(m);
    draw_field_menu_label_core(m);
    draw_centered_option_panel_call_core(m);
    dma_sprite_table_core(m);
    m.call_far(0x4000, 0x0009);
    dma_palettes_core(m);
branch_6e9a:;
    m.call_far(0x4000, 0x005C);
    m.ax() = uint16_t(m.rd16(m.ds(), uint16_t(kKeysRepeat)));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x1));
    m.si() = uint16_t(kSaveChoice);
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x8));
    if (!(m.flags() & emu::ZF))
        goto branch_6ed2;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x4));
    if (!(m.flags() & emu::ZF))
        goto branch_6eda;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x80));
    if (!(m.flags() & emu::ZF))
        goto branch_6ef6;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x20));
    if (!(m.flags() & emu::ZF))
        goto branch_6f01;
branch_6ebb:;
    m.di() = uint16_t(0x6F18);
    draw_options_primary_cursor_call_core(m);
    dma_sprite_table_core(m);
    dma_palettes_core(m);
    m.call_far(0x4000, 0x0009);
    goto branch_6e9a;
branch_6ed2:;
    m.ax() = uint16_t(0x94);
    trigger_sound_effect_core(m);
    goto branch_6f17;
branch_6eda:;
    m.ax() = uint16_t(0x93);
    trigger_sound_effect_core(m);
    m.bx() = uint16_t(
        (m.bx() & 0x00FF) |
        (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx() >> 8) ^ uint8_t(m.bx() >> 8)))))
         << 8));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.bx() = uint16_t(computed::shift_left<uint16_t>(m, m.bx(), 0x1));
    m.ax() = uint16_t(m.rd16(m.cs(), uint16_t(m.bx() + 0x6EF2)));
    {
        const uint16_t callback_cs = m.cs();
        dispatch_computed_handler_core(m, m.ax());
        if (m.cs() != callback_cs)
            return;
    }
    if (m.flags() & emu::CF)
        goto branch_6f17;
    goto branch_6e7f;
branch_6ef6:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x1)));
    if (!(m.flags() & emu::CF))
        goto branch_6f0c;
    m.set_al(uint8_t(uint8_t(m.dx())));
    goto branch_6f0c;
branch_6f01:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.dx()));
    if (m.flags() & (emu::CF | emu::ZF))
        goto branch_6f0c;
    m.set_al(uint8_t(0x0));
branch_6f0c:;
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    m.ax() = uint16_t(0x92);
    trigger_sound_effect_core(m);
    goto branch_6ebb;
branch_6f17:;
    return;
}
void run_monster_preview_setting_menu(Machine &m) {
    run_monster_preview_setting_menu_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x346F1C (near)
// Clear battle option bit 3.
void disable_monster_preview_core(Machine &m) {
    m.wr8(m.ss(), uint16_t(kOptions),
          uint8_t(computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ss(), uint16_t(kOptions)) & 0xF7))));
    m.flags() |= emu::CF;
    return;
}
void disable_monster_preview(Machine &m) {
    disable_monster_preview_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x346F24 (near)
// Set battle option bit 3.
void enable_monster_preview_core(Machine &m) {
    m.wr8(m.ss(), uint16_t(kOptions),
          uint8_t(computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ss(), uint16_t(kOptions)) & 0xF7))));
    m.wr8(m.ss(), uint16_t(kOptions),
          uint8_t(computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ss(), uint16_t(kOptions)) | 0x8))));
    m.flags() |= emu::CF;
    return;
}
void enable_monster_preview(Machine &m) {
    enable_monster_preview_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x346F32 (near)
// Select and store the dialogue delay from zero through seven.
void run_dialogue_speed_menu_core(Machine &m) {
    m.ax() = uint16_t(0x93);
    trigger_sound_effect_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kDialogueSpeed))));
    m.wr8(m.ss(), uint16_t(kSaveChoice), uint8_t(m.al()));
    m.di() = uint16_t(0x6FCD);
    draw_options_primary_cursor_call_core(m);
    draw_field_menu_label_core(m);
    draw_dialogue_speed_panel_call_core(m);
    dma_sprite_table_core(m);
    m.call_far(0x4000, 0x0009);
    dma_palettes_core(m);
branch_6f5b:;
    m.call_far(0x4000, 0x005C);
    m.ax() = uint16_t(m.rd16(m.ds(), uint16_t(kKeysRepeat)));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x7));
    m.si() = uint16_t(kSaveChoice);
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x8));
    if (!(m.flags() & emu::ZF))
        goto branch_6f93;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x4));
    if (!(m.flags() & emu::ZF))
        goto branch_6f9b;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x80));
    if (!(m.flags() & emu::ZF))
        goto branch_6fab;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x20));
    if (!(m.flags() & emu::ZF))
        goto branch_6fb6;
branch_6f7c:;
    m.di() = uint16_t(0x6FCD);
    draw_options_primary_cursor_call_core(m);
    dma_sprite_table_core(m);
    dma_palettes_core(m);
    m.call_far(0x4000, 0x0009);
    goto branch_6f5b;
branch_6f93:;
    m.ax() = uint16_t(0x94);
    trigger_sound_effect_core(m);
    goto branch_6fcc;
branch_6f9b:;
    m.ax() = uint16_t(0x93);
    trigger_sound_effect_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.wr8(m.ss(), uint16_t(kDialogueSpeed), uint8_t(m.al()));
    m.flags() |= emu::CF;
    goto branch_6fcc;
branch_6fab:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x1)));
    if (!(m.flags() & emu::CF))
        goto branch_6fc1;
    m.set_al(uint8_t(uint8_t(m.dx())));
    goto branch_6fc1;
branch_6fb6:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.dx()));
    if (m.flags() & (emu::CF | emu::ZF))
        goto branch_6fc1;
    m.set_al(uint8_t(0x0));
branch_6fc1:;
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    m.ax() = uint16_t(0x92);
    trigger_sound_effect_core(m);
    goto branch_6f7c;
branch_6fcc:;
    return;
}
void run_dialogue_speed_menu(Machine &m) {
    run_dialogue_speed_menu_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x346FDD (near)
// Select which party member is assigned to each dialogue position.
void run_dialogue_party_order_menu_core(Machine &m) {
    m.ax() = uint16_t(0x93);
    trigger_sound_effect_core(m);
    m.set_al(uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kSaveChoice), uint8_t(m.al()));
branch_6fe9:;
    m.di() = uint16_t(0x7071);
    draw_options_primary_cursor_call_core(m);
    draw_field_menu_label_core(m);
    draw_dialogue_party_order_panel_call_core(m);
    dma_sprite_table_core(m);
    m.call_far(0x4000, 0x0009);
    dma_palettes_core(m);
branch_7004:;
    m.call_far(0x4000, 0x005C);
    m.ax() = uint16_t(m.rd16(m.ds(), uint16_t(kKeysRepeat)));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x3));
    m.si() = uint16_t(kSaveChoice);
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x8));
    if (!(m.flags() & emu::ZF))
        goto branch_703c;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x4));
    if (!(m.flags() & emu::ZF))
        goto branch_7044;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x10));
    if (!(m.flags() & emu::ZF))
        goto branch_704f;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x40));
    if (!(m.flags() & emu::ZF))
        goto branch_705a;
branch_7025:;
    m.di() = uint16_t(0x7071);
    draw_options_primary_cursor_call_core(m);
    dma_sprite_table_core(m);
    dma_palettes_core(m);
    m.call_far(0x4000, 0x0009);
    goto branch_7004;
branch_703c:;
    m.ax() = uint16_t(0x94);
    trigger_sound_effect_core(m);
    goto branch_7070;
branch_7044:;
    m.ax() = uint16_t(0x93);
    trigger_sound_effect_core(m);
    select_field_menu_assignment_core(m);
    goto branch_6fe9;
branch_704f:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x1)));
    if (!(m.flags() & emu::CF))
        goto branch_7065;
    m.set_al(uint8_t(uint8_t(m.dx())));
    goto branch_7065;
branch_705a:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.dx()));
    if (m.flags() & (emu::CF | emu::ZF))
        goto branch_7065;
    m.set_al(uint8_t(0x0));
branch_7065:;
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    m.ax() = uint16_t(0x92);
    trigger_sound_effect_core(m);
    goto branch_7025;
branch_7070:;
    return;
}
void run_dialogue_party_order_menu(Machine &m) {
    run_dialogue_party_order_menu_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x347138 (near)
// Reset the dialogue party order and play the confirmation sound.
void reset_dialogue_order_with_sound_core(Machine &m) {
    m.ax() = uint16_t(0x11);
    trigger_sound_effect_core(m);
    reset_dialogue_party_order_call_core(m);
    return;
}
void reset_dialogue_order_with_sound(Machine &m) {
    reset_dialogue_order_with_sound_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x347747 (near)
// Browse save slots and load the confirmed slot into game state.
void run_load_tab_core(Machine &m) {
    UiSceneScope ui_scope(m, hooks::Event::SaveMenu, 1);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.set_al(uint8_t(0x0));
    m.wr8(m.ds(), uint16_t(kSaveSlotSelection), uint8_t(m.al()));
    m.wr8(m.ds(), uint16_t(kSaveChoice), uint8_t(m.al()));
    m.wr8(m.ds(), uint16_t(kMenuExitRequested), uint8_t(m.al()));
    m.wr8(m.ds(), uint16_t(kMenuActionResult), uint8_t(m.al()));
    read_last_save_slot_core(m);
    m.wr8(m.ds(), uint16_t(kSaveSlotSelection), uint8_t(m.al()));
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0x0);
    m.cx() = uint16_t(0x2020);
    m.dx() = uint16_t(0x1FF);
    fill_screen_rect_core(m);
    m.ax() = uint16_t(0x7);
    computed::out_word(m, uint8_t(0x0), m.ax());
    m.ax() = uint16_t(0x8);
    computed::out_word(m, uint8_t(0x10), m.ax());
    m.ax() = uint16_t(0xFC08);
    computed::out_word(m, uint8_t(0x12), m.ax());
    m.set_al(uint8_t(0x32));
    m.out8(uint8_t(0x7), m.al());
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x5), m.al());
    m.set_al(uint8_t(0x20));
    m.out8(uint8_t(0x6), m.al());
branch_778c:;
    m.di() = uint16_t(0x784E);
    draw_load_menu_cursor_call_core(m);
    draw_load_menu_screen_call_core(m);
    draw_selected_save_party_core(m);
    dma_sprite_table_core(m);
    m.call_far(0x4000, 0x0009);
    dma_palettes_core(m);
branch_77a7:;
    m.call_far(0x4000, 0x005C);
    m.ax() = uint16_t(m.rd16(m.ds(), uint16_t(kKeysRepeat)));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x2));
    m.si() = uint16_t(kSaveSlotSelection);
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x8));
    if (m.flags() & emu::ZF)
        goto branch_77bc;
    goto branch_7842;
branch_77bc:;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x4));
    if (!(m.flags() & emu::ZF))
        goto branch_77ee;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x10));
    if (!(m.flags() & emu::ZF))
        goto branch_781e;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x40));
    if (!(m.flags() & emu::ZF))
        goto branch_7829;
branch_77cb:;
    m.di() = uint16_t(0x784E);
    draw_load_menu_cursor_call_core(m);
    dma_sprite_table_core(m);
    dma_palettes_core(m);
    m.call_far(0x4000, 0x0009);
    goto branch_77a7;
branch_77ee:;
    m.ax() = uint16_t(0x93);
    trigger_sound_effect_core(m);
    confirm_load_slot_core(m);
    if (m.flags() & emu::CF)
        goto branch_784d;
    goto branch_778c;
branch_781e:;
    if (SaveSlots *slots = extended_save_slots(m)) { // Save Slots 9: scroll, don't wrap
        if (scroll_save_list(m, *slots, false)) {    // scrolled: redraw the list
            m.ax() = uint16_t(0x92);
            trigger_sound_effect_core(m);
            goto branch_778c;
        }
        goto branch_7834_extended;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x1)));
    if (!(m.flags() & emu::CF))
        goto branch_7834;
    m.set_al(uint8_t(uint8_t(m.dx())));
    goto branch_7834;
branch_7829:;
    if (SaveSlots *slots = extended_save_slots(m)) {
        if (scroll_save_list(m, *slots, true)) { // scrolled: redraw the list
            m.ax() = uint16_t(0x92);
            trigger_sound_effect_core(m);
            goto branch_778c;
        }
        goto branch_7834_extended;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.dx()));
    if (m.flags() & (emu::CF | emu::ZF))
        goto branch_7834;
    m.set_al(uint8_t(0x0));
branch_7834:;
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
branch_7834_extended:;
    draw_selected_save_party_core(m);
    m.ax() = uint16_t(0x92);
    trigger_sound_effect_core(m);
    goto branch_77cb;
branch_7842:;
    m.set_al(uint8_t(0x1));
    m.wr8(m.ds(), uint16_t(kMenuActionResult), uint8_t(m.al()));
    m.ax() = uint16_t(0x94);
    trigger_sound_effect_core(m);
branch_784d:;
    return;
}
void run_load_tab(Machine &m) {
    run_load_tab_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x348284 (near)
// Return without drawing an item description for this menu mode.
void ignore_menu_item_description_core(Machine &m) {
    (void)m;
    return;
}
void ignore_menu_item_description(Machine &m) {
    ignore_menu_item_description_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x348295 (near)
// Draw the selected inventory or overflow-item description.
void draw_inventory_item_description_core(Machine &m) {
    TextKey item_desc_key{};
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(kSaveSlotSelection))));
    m.bx() = uint16_t(kInventory);
    (void)computed::subtract<uint8_t>(m, m.rd8(m.ds(), uint16_t(kCreationLeader)), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_82a8;
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(kSaveChoice))));
    m.bx() = uint16_t(kKeyItems);
branch_82a8:;
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x1));
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), m.ax()));
    m.di() = uint16_t(kItemDescriptionText);
    (void)computed::subtract<uint8_t>(m, m.rd8(m.ds(), uint16_t(kCreationLeader)), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_82e8;
    (void)computed::subtract<uint8_t>(m, m.rd8(m.ds(), uint16_t(kSaveSlotSelection)), 0x8);
    if (m.flags() & emu::ZF)
        goto branch_82c8;
    (void)computed::subtract<uint8_t>(m, m.rd8(m.ds(), uint16_t(kSaveSlotSelection)), 0x9);
    if (m.flags() & emu::ZF)
        goto branch_82cd;
    goto branch_82e8;
branch_82c8:;
    item_desc_key = TextKey::draw_inventory_item_description_1;
    goto branch_82d2;
branch_82cd:;
    item_desc_key = TextKey::draw_inventory_item_description_2;
    goto branch_82d2;
branch_82d2:;
    {
        const TextPtr text_ptr = text(m, item_desc_key);
        m.si() = text_ptr.offset;
        computed::push(m, m.es());
        computed::push(m, m.ds());
        m.ax() = uint16_t(text_ptr.segment);
        m.ds() = uint16_t(m.ax());
        m.ax() = uint16_t(0x0);
        m.es() = uint16_t(m.ax());
        m.cx() = uint16_t(0x1A);
        copy_bytes_terminated_core(m);
        text_done(m, item_desc_key);
        m.ds() = uint16_t(computed::pop(m));
        m.es() = uint16_t(computed::pop(m));
    }
    goto branch_8319;
branch_82e8:;
    m.si() = uint16_t(0x53E0);
    m.ax() = uint16_t(m.rd16(m.ds(), uint16_t(m.bx())));
    classify_item_entry_call_core(m);
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_82fa;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.bx()))));
    goto branch_82fe;
branch_82fa:;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(0xFF));
branch_82fe:;
    m.dx() = uint16_t(0x1B);
    {
        const uint32_t product = uint32_t(m.ax()) * m.dx();
        m.ax() = uint16_t(product);
        m.dx() = uint16_t(product >> 16);
        computed::flag(m, emu::CF | emu::OF, m.dx() != 0);
    }
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.ax()));
    computed::push(m, m.es());
    computed::push(m, m.ds());
    m.ax() = uint16_t(0x9000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.cx() = uint16_t(0x1A);
    copy_bytes_terminated_core(m);
    m.ds() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
branch_8319:;
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0x1002);
    m.si() = uint16_t(kItemDescriptionText);
    draw_text_core(m);
    return;
}
void draw_inventory_item_description(Machine &m) {
    draw_inventory_item_description_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x348328 (near)
// Draw the selected member equipment or inventory-item description.
void draw_equipment_item_description_core(Machine &m) {
    TextKey item_desc_key{};
    m.bx() = uint16_t(kGameState);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kCreationRace))));
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x1F));
    {
        const uint16_t product = uint16_t(m.al()) * uint8_t(m.dx());
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), m.ax()));
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), 0xF));
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(kSaveSlotSelection))));
    (void)computed::subtract<uint8_t>(m, m.rd8(m.ds(), uint16_t(kCreationLeader)), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_834a;
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(kSaveChoice))));
    m.bx() = uint16_t(kInventory);
branch_834a:;
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x1));
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), m.ax()));
    m.di() = uint16_t(kItemDescriptionText);
    (void)computed::subtract<uint8_t>(m, m.rd8(m.ds(), uint16_t(kCreationLeader)), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_838a;
    (void)computed::subtract<uint8_t>(m, m.rd8(m.ds(), uint16_t(kSaveSlotSelection)), 0x9);
    if (m.flags() & emu::ZF)
        goto branch_836a;
    (void)computed::subtract<uint8_t>(m, m.rd8(m.ds(), uint16_t(kSaveSlotSelection)), 0x8);
    if (m.flags() & emu::ZF)
        goto branch_836f;
    goto branch_838a;
branch_836a:;
    item_desc_key = TextKey::draw_equipment_item_description_1;
    goto branch_8374;
branch_836f:;
    item_desc_key = TextKey::draw_equipment_item_description_2;
    goto branch_8374;
branch_8374:;
    {
        const TextPtr text_ptr = text(m, item_desc_key);
        m.si() = text_ptr.offset;
        computed::push(m, m.es());
        computed::push(m, m.ds());
        m.ax() = uint16_t(text_ptr.segment);
        m.ds() = uint16_t(m.ax());
        m.ax() = uint16_t(0x0);
        m.es() = uint16_t(m.ax());
        m.cx() = uint16_t(0x1A);
        copy_bytes_terminated_core(m);
        text_done(m, item_desc_key);
        m.ds() = uint16_t(computed::pop(m));
        m.es() = uint16_t(computed::pop(m));
    }
    goto branch_83bb;
branch_838a:;
    m.si() = uint16_t(0x53E0);
    m.ax() = uint16_t(m.rd16(m.ds(), uint16_t(m.bx())));
    classify_item_entry_call_core(m);
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_839c;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.bx()))));
    goto branch_83a0;
branch_839c:;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(0xFF));
branch_83a0:;
    m.dx() = uint16_t(0x1B);
    {
        const uint32_t product = uint32_t(m.ax()) * m.dx();
        m.ax() = uint16_t(product);
        m.dx() = uint16_t(product >> 16);
        computed::flag(m, emu::CF | emu::OF, m.dx() != 0);
    }
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.ax()));
    computed::push(m, m.es());
    computed::push(m, m.ds());
    m.ax() = uint16_t(0x9000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.cx() = uint16_t(0x1A);
    copy_bytes_terminated_core(m);
    m.ds() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
branch_83bb:;
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0x1002);
    m.si() = uint16_t(kItemDescriptionText);
    draw_text_core(m);
    return;
}
void draw_equipment_item_description(Machine &m) {
    draw_equipment_item_description_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x3483CA (near)
// Draw an equipment description unless the member is a monster or the item is invalid.
void draw_usable_equipment_description_core(Machine &m) {
    TextKey item_desc_key{};
    m.bx() = uint16_t(kGameState);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kCreationRace))));
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x1F));
    {
        const uint16_t product = uint16_t(m.al()) * uint8_t(m.dx());
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), m.ax()));
    (void)computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ss(), uint16_t(m.bx() + 0x000A)) & 0x40));
    if (!(m.flags() & emu::ZF))
        goto branch_844f;
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), 0xF));
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(kSaveSlotSelection))));
    (void)computed::subtract<uint8_t>(m, m.rd8(m.ds(), uint16_t(kCreationLeader)), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_83fd;
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(kSaveChoice))));
    m.bx() = uint16_t(kInventory);
    (void)computed::subtract<uint8_t>(m, m.rd8(m.ds(), uint16_t(kNameInsertionIndex)), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_83fd;
    m.bx() = uint16_t(kKeyItems);
branch_83fd:;
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x1));
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), m.ax()));
    m.di() = uint16_t(kItemDescriptionText);
    (void)computed::subtract<uint8_t>(m, m.rd8(m.ds(), uint16_t(kCreationLeader)), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_843d;
    (void)computed::subtract<uint8_t>(m, m.rd8(m.ds(), uint16_t(kSaveSlotSelection)), 0x9);
    if (m.flags() & emu::ZF)
        goto branch_841d;
    (void)computed::subtract<uint8_t>(m, m.rd8(m.ds(), uint16_t(kSaveSlotSelection)), 0x8);
    if (m.flags() & emu::ZF)
        goto branch_8422;
    goto branch_843d;
branch_841d:;
    item_desc_key = TextKey::draw_usable_equipment_description_1;
    goto branch_8427;
branch_8422:;
    item_desc_key = TextKey::draw_usable_equipment_description_2;
    goto branch_8427;
branch_8427:;
    {
        const TextPtr text_ptr = text(m, item_desc_key);
        m.si() = text_ptr.offset;
        computed::push(m, m.es());
        computed::push(m, m.ds());
        m.ax() = uint16_t(text_ptr.segment);
        m.ds() = uint16_t(m.ax());
        m.ax() = uint16_t(0x0);
        m.es() = uint16_t(m.ax());
        m.cx() = uint16_t(0x1A);
        copy_bytes_terminated_core(m);
        text_done(m, item_desc_key);
        m.ds() = uint16_t(computed::pop(m));
        m.es() = uint16_t(computed::pop(m));
    }
    goto branch_8474;
branch_843d:;
    m.si() = uint16_t(0x53E0);
    m.ax() = uint16_t(m.rd16(m.ds(), uint16_t(m.bx())));
    classify_item_entry_call_core(m);
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_844f;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.bx()))));
    goto branch_8459;
branch_844f:;
    m.si() = uint16_t(0x53E0);
    m.di() = uint16_t(kItemDescriptionText);
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(0xFF));
branch_8459:;
    m.dx() = uint16_t(0x1B);
    {
        const uint32_t product = uint32_t(m.ax()) * m.dx();
        m.ax() = uint16_t(product);
        m.dx() = uint16_t(product >> 16);
        computed::flag(m, emu::CF | emu::OF, m.dx() != 0);
    }
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.ax()));
    computed::push(m, m.es());
    computed::push(m, m.ds());
    m.ax() = uint16_t(0x9000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.es() = uint16_t(m.ax());
    m.cx() = uint16_t(0x1A);
    copy_bytes_terminated_core(m);
    m.ds() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
branch_8474:;
    computed::push(m, m.ds());
    computed::push(m, m.ss());
    m.ds() = uint16_t(computed::pop(m));
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0x1002);
    m.si() = uint16_t(kItemDescriptionText);
    draw_text_core(m);
    m.ds() = uint16_t(computed::pop(m));
    return;
}
void draw_usable_equipment_description(Machine &m) {
    draw_usable_equipment_description_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x348487 (near)
// Draw the description of the selected inventory command.
void draw_inventory_command_description_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(kSaveSlotSelection))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::ZF)
        goto branch_84ae;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::ZF)
        goto branch_84a9;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::ZF)
        goto branch_84a4;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_849f;
    {
        const TextPtr text_ptr = text(m, TextKey::draw_inventory_command_description_1);
        m.ds() = text_ptr.segment;
        m.si() = text_ptr.offset;
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t(0x1002);
        draw_text_core(m);
        text_done(m, TextKey::draw_inventory_command_description_1);
    }
    return;
branch_849f:;
    {
        const TextPtr text_ptr = text(m, TextKey::draw_inventory_command_description_2);
        m.ds() = text_ptr.segment;
        m.si() = text_ptr.offset;
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t(0x1002);
        draw_text_core(m);
        text_done(m, TextKey::draw_inventory_command_description_2);
    }
    return;
branch_84a4:;
    {
        const TextPtr text_ptr = text(m, TextKey::draw_inventory_command_description_3);
        m.ds() = text_ptr.segment;
        m.si() = text_ptr.offset;
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t(0x1002);
        draw_text_core(m);
        text_done(m, TextKey::draw_inventory_command_description_3);
    }
    return;
branch_84a9:;
    {
        const TextPtr text_ptr = text(m, TextKey::draw_inventory_command_description_4);
        m.ds() = text_ptr.segment;
        m.si() = text_ptr.offset;
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t(0x1002);
        draw_text_core(m);
        text_done(m, TextKey::draw_inventory_command_description_4);
    }
    return;
branch_84ae:;
    {
        const TextPtr text_ptr = text(m, TextKey::field_menu_label_3); // D_34BB60
        m.si() = text_ptr.offset;
        m.ds() = text_ptr.segment;
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t(0x1002);
        draw_text_core(m);
        text_done(m, TextKey::field_menu_label_3);
    }
    return;
}
void draw_inventory_command_description(Machine &m) {
    draw_inventory_command_description_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x348526
// Play AX's sound effect while preserving all registers and segments.
void play_menu_sound_preserving_registers_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void play_menu_sound_preserving_registers(Machine &m) {
    play_menu_sound_preserving_registers_core(m);
    if (m.cs() == 0x4000)
        m.retf();
}
// rom: 0x348534
// Draw the strength-increase message with a two-digit amount.
void show_strength_increase_message_core(Machine &m) {
    hooks::emit(hooks::Event::ItemMessage, m, {3, int(TextKey::show_strength_increase_message_1), m.bx()});
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    computed::push(m, m.cs());
    m.ds() = uint16_t(computed::pop(m));
    computed::push(m, m.bx());
    const uint16_t amount = m.bx(); // {amount} (text_tokens.h): in the string, not drawn over its ０
    {
        const TextPtr text_ptr = text(m, TextKey::show_strength_increase_message_1);
        m.ds() = text_ptr.segment;
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t(0x1002);
        m.si() = text_ptr.offset;
        TextValues values;
        values.set(U"amount", decimal_text(amount, 2));
        draw_text_core(m);
        text_done(m, TextKey::show_strength_increase_message_1);
    }
    m.bx() = uint16_t(computed::pop(m));
    m.dx() = uint16_t(m.bx());
    decimal_to_bcd_clamped_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0x1008);
    m.cx() = uint16_t(0x2);
    if (!has_value_tokens(m, TextKey::show_strength_increase_message_1))
        draw_bcd_number_core(m);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_strength_increase_message(Machine &m) {
    show_strength_increase_message_core(m);
    if (m.cs() == 0x4000)
        m.retf();
}
// rom: 0x348560
// Draw the agility-increase message with a two-digit amount.
void show_agility_increase_message_core(Machine &m) {
    hooks::emit(hooks::Event::ItemMessage, m, {3, int(TextKey::show_agility_increase_message_1), m.bx()});
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    computed::push(m, m.cs());
    m.ds() = uint16_t(computed::pop(m));
    computed::push(m, m.bx());
    const uint16_t amount = m.bx(); // {amount} (text_tokens.h): in the string, not drawn over its ０
    {
        const TextPtr text_ptr = text(m, TextKey::show_agility_increase_message_1);
        m.ds() = text_ptr.segment;
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t(0x1002);
        m.si() = text_ptr.offset;
        TextValues values;
        values.set(U"amount", decimal_text(amount, 2));
        draw_text_core(m);
        text_done(m, TextKey::show_agility_increase_message_1);
    }
    m.bx() = uint16_t(computed::pop(m));
    m.dx() = uint16_t(m.bx());
    decimal_to_bcd_clamped_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0x1008);
    m.cx() = uint16_t(0x2);
    if (!has_value_tokens(m, TextKey::show_agility_increase_message_1))
        draw_bcd_number_core(m);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_agility_increase_message(Machine &m) {
    show_agility_increase_message_core(m);
    if (m.cs() == 0x4000)
        m.retf();
}
// rom: 0x34858C
// Draw the HP message with a three-digit amount at column seven.
void show_max_hp_increase_message_core(Machine &m) {
    hooks::emit(hooks::Event::ItemMessage, m, {3, int(TextKey::show_max_hp_increase_message_1), m.bx()});
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    computed::push(m, m.cs());
    m.ds() = uint16_t(computed::pop(m));
    computed::push(m, m.bx());
    const uint16_t amount = m.bx(); // {amount} (text_tokens.h): in the string, not drawn over its ０
    {
        const TextPtr text_ptr = text(m, TextKey::show_max_hp_increase_message_1);
        m.ds() = text_ptr.segment;
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t(0x1002);
        m.si() = text_ptr.offset;
        TextValues values;
        values.set(U"amount", decimal_text(amount, 3));
        draw_text_core(m);
        text_done(m, TextKey::show_max_hp_increase_message_1);
    }
    m.bx() = uint16_t(computed::pop(m));
    m.dx() = uint16_t(m.bx());
    decimal_to_bcd_clamped_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0x1007);
    m.cx() = uint16_t(0x3);
    if (!has_value_tokens(m, TextKey::show_max_hp_increase_message_1))
        draw_bcd_number_core(m);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_max_hp_increase_message(Machine &m) {
    show_max_hp_increase_message_core(m);
    if (m.cs() == 0x4000)
        m.retf();
}
// rom: 0x348642 (near)
// Apply the field effect of potion through its bank 35 handler.
void dispatch_field_potion_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        use_field_potion_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void dispatch_field_potion(Machine &m) {
    dispatch_field_potion_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x348648 (near)
// Apply the field effect of hi potion through its bank 35 handler.
void dispatch_field_hi_potion_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        use_field_hi_potion_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void dispatch_field_hi_potion(Machine &m) {
    dispatch_field_hi_potion_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x34864E (near)
// Apply the field effect of cure book through its bank 35 handler.
void dispatch_field_cure_book_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        use_field_cure_book_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void dispatch_field_cure_book(Machine &m) {
    dispatch_field_cure_book_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x348654 (near)
// Apply the field effect of healing staff through its bank 35 handler.
void dispatch_field_healing_staff_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        use_field_healing_staff_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void dispatch_field_healing_staff(Machine &m) {
    dispatch_field_healing_staff_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x34865A (near)
// Apply the field effect of gold needle through its bank 35 handler.
void dispatch_field_gold_needle_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        use_field_gold_needle_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void dispatch_field_gold_needle(Machine &m) {
    dispatch_field_gold_needle_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x348660 (near)
// Apply the field effect of cross through its bank 35 handler.
void dispatch_field_cross_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        use_field_cross_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void dispatch_field_cross(Machine &m) {
    dispatch_field_cross_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x348666 (near)
// Apply the field effect of eye drops through its bank 35 handler.
void dispatch_field_eye_drops_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        use_field_eye_drops_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void dispatch_field_eye_drops(Machine &m) {
    dispatch_field_eye_drops_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x34866C (near)
// Apply the field effect of revive item through its bank 35 handler.
void dispatch_field_revive_item_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        use_field_revive_item_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void dispatch_field_revive_item(Machine &m) {
    dispatch_field_revive_item_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x348672 (near)
// Apply the field effect of elixir through its bank 35 handler.
void dispatch_field_elixir_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        use_field_elixir_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void dispatch_field_elixir(Machine &m) {
    dispatch_field_elixir_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x348678 (near)
// Apply the field effect of heart through its bank 35 handler.
void dispatch_field_heart_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        use_field_heart_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void dispatch_field_heart(Machine &m) {
    dispatch_field_heart_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x34867E (near)
// Apply the field effect of sages stone through its bank 35 handler.
void dispatch_field_sages_stone_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        use_field_sages_stone_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void dispatch_field_sages_stone(Machine &m) {
    dispatch_field_sages_stone_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x348684 (near)
// Apply the field effect of door through its bank 35 handler.
void dispatch_field_door_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        use_destination_travel_item_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void dispatch_field_door(Machine &m) {
    dispatch_field_door_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x34868A (near)
// Apply the field effect of strength source through its bank 35 handler.
void dispatch_field_strength_source_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        use_field_strength_source_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void dispatch_field_strength_source(Machine &m) {
    dispatch_field_strength_source_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x348690 (near)
// Apply the field effect of agility source through its bank 35 handler.
void dispatch_field_agility_source_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        use_field_agility_source_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void dispatch_field_agility_source(Machine &m) {
    dispatch_field_agility_source_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x348696 (near)
// Apply the field effect of hp two hundred through its bank 35 handler.
void dispatch_field_hp_two_hundred_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        use_field_hp_two_hundred_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void dispatch_field_hp_two_hundred(Machine &m) {
    dispatch_field_hp_two_hundred_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x34869C (near)
// Apply the field effect of hp four hundred through its bank 35 handler.
void dispatch_field_hp_four_hundred_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        use_field_hp_four_hundred_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void dispatch_field_hp_four_hundred(Machine &m) {
    dispatch_field_hp_four_hundred_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x3486A2 (near)
// Apply the field effect of hp six hundred through its bank 35 handler.
void dispatch_field_hp_six_hundred_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        use_field_hp_six_hundred_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void dispatch_field_hp_six_hundred(Machine &m) {
    dispatch_field_hp_six_hundred_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x3486A8 (near)
// Apply the field effect of first tower key through its bank 35 handler.
void dispatch_field_first_tower_key_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        use_first_tower_key_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void dispatch_field_first_tower_key(Machine &m) {
    dispatch_field_first_tower_key_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x3486AE (near)
// Apply the field effect of second tower key through its bank 35 handler.
void dispatch_field_second_tower_key_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        use_second_tower_key_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void dispatch_field_second_tower_key(Machine &m) {
    dispatch_field_second_tower_key_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x3486B4 (near)
// Apply the field effect of third tower key through its bank 35 handler.
void dispatch_field_third_tower_key_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        use_third_tower_key_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void dispatch_field_third_tower_key(Machine &m) {
    dispatch_field_third_tower_key_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x3486BA (near)
// Apply the field effect of antidote through its bank 35 handler.
void dispatch_field_antidote_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        use_field_antidote_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void dispatch_field_antidote(Machine &m) {
    dispatch_field_antidote_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x3486C0 (near)
// Apply the field effect of wake bell through its bank 35 handler.
void dispatch_field_wake_bell_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        use_field_wake_bell_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void dispatch_field_wake_bell(Machine &m) {
    dispatch_field_wake_bell_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x3486C6 (near)
// Apply the field effect of frying pan through its bank 35 handler.
void dispatch_field_frying_pan_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        use_field_frying_pan_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void dispatch_field_frying_pan(Machine &m) {
    dispatch_field_frying_pan_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x3486CC (near)
// Apply the field effect of paralysis cure through its bank 35 handler.
void dispatch_field_paralysis_cure_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        use_field_paralysis_cure_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void dispatch_field_paralysis_cure(Machine &m) {
    dispatch_field_paralysis_cure_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x3486D2 (near)
// Apply the field effect of nectar through its bank 35 handler.
void dispatch_field_nectar_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        use_field_nectar_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void dispatch_field_nectar(Machine &m) {
    dispatch_field_nectar_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x3486D8 (near)
// Apply the field effect of cure spell through its bank 35 handler.
void dispatch_field_cure_spell_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        use_field_cure_spell_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void dispatch_field_cure_spell(Machine &m) {
    dispatch_field_cure_spell_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x3486DE (near)
// Apply the field effect of healing spell through its bank 35 handler.
void dispatch_field_healing_spell_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        use_field_healing_spell_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void dispatch_field_healing_spell(Machine &m) {
    dispatch_field_healing_spell_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x3486E4 (near)
// Apply the field effect of revival spell through its bank 35 handler.
void dispatch_field_revival_spell_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        use_field_revival_spell_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void dispatch_field_revival_spell(Machine &m) {
    dispatch_field_revival_spell_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x3486EA (near)
// Apply the field effect of treatment spell through its bank 35 handler.
void dispatch_field_treatment_spell_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        use_field_treatment_spell_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void dispatch_field_treatment_spell(Machine &m) {
    dispatch_field_treatment_spell_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x3486F0 (near)
// Apply the field effect of raise spell through its bank 35 handler.
void dispatch_field_raise_spell_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        use_field_raise_spell_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void dispatch_field_raise_spell(Machine &m) {
    dispatch_field_raise_spell_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x3486F6 (near)
// Apply the field effect of teleport spell through its bank 35 handler.
void dispatch_field_teleport_spell_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        ignore_field_teleport_spell_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void dispatch_field_teleport_spell(Machine &m) {
    dispatch_field_teleport_spell_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x348765 (near)
// Browse the monster transformation history of eligible party members.
void run_monster_history_menu_core(Machine &m) {
    // A host picture may cover the screens (hooks::Event::Bestiary); every return hides it.
    struct Leave {
        Machine &m;
        ~Leave() { hooks::emit(hooks::Event::Bestiary, m, {0}); }
    } const presentation{m};
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.wr8(m.ss(), uint16_t(kMenuSixthSelection), uint8_t(m.al()));
    m.wr16(m.ss(), uint16_t(kCreationRace), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(kNameInsertionIndex), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(kMenuSecondItemBuffer), uint16_t(m.ax()));
    m.si() = uint16_t(kMenuFifthSelection);
    select_next_history_member_call_core(m);
    m.set_al(uint8_t(m.in8(uint8_t(0x6))));
    m.wr8(m.ss(), uint16_t(kMenuTransferState), uint8_t(m.al()));
    (void)computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ss(), uint16_t(kMemberEquipmentFlags)) & 0x40));
    if (!(m.flags() & emu::ZF))
        goto gate_open;
    (void)computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ss(), uint16_t(kSecondMemberEquipmentFlags)) & 0x40));
    if (!(m.flags() & emu::ZF))
        goto gate_open;
    (void)computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ss(), uint16_t(kThirdMemberEquipmentFlags)) & 0x40));
    if (!(m.flags() & emu::ZF))
        goto gate_open;
    (void)computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ss(), uint16_t(kFourthMemberEquipmentFlags)) & 0x40));
    if (!(m.flags() & emu::ZF))
        goto gate_open;
    // The host may overrule the gate (hooks::VB::BestiaryHistory); nobody asked: the original's answer.
    if (hooks::should(hooks::VB::BestiaryHistory, m, false))
        goto branch_87f3;
    goto notice;
gate_open:;
    if (hooks::should(hooks::VB::BestiaryHistory, m, true))
        goto branch_87f3;
notice:;
    hooks::emit(hooks::Event::Bestiary, m, {3});
    m.ax() = uint16_t(0x2A);
    trigger_sound_effect_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0x708);
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xD));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x4)) << 8));
    draw_window_core(m);
    {
        const TextPtr text_ptr = text(m, TextKey::run_monster_history_menu_1);
        m.ds() = text_ptr.segment;
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t(0x80A);
        m.si() = text_ptr.offset;
        draw_text_core(m);
        text_done(m, TextKey::run_monster_history_menu_1);
    }
    {
        const TextPtr text_ptr = text(m, TextKey::run_monster_history_menu_2);
        m.ds() = text_ptr.segment;
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t(0x909);
        m.si() = text_ptr.offset;
        draw_text_core(m);
        text_done(m, TextKey::run_monster_history_menu_2);
    }
branch_87de:;
    m.call_far(0x4000, 0x0009);
    m.call_far(0x4000, 0x005C);
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kKeysPressed)));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | m.ax())));
    if (m.flags() & emu::ZF)
        goto branch_87de;
    goto branch_8893;
branch_87f3:;
    draw_bestiary_category_cursor_core(m);
    draw_monster_history_screen_call_core(m);
    draw_selected_monster_history_call_core(m);
    hooks::emit(hooks::Event::Bestiary, m, {1});
    dma_sprite_table_core(m);
    m.call_far(0x4000, 0x0009);
    dma_palettes_core(m);
branch_880b:;
    m.call_far(0x4000, 0x005C);
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kKeysRepeat)));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x3));
    m.si() = uint16_t(kMenuFifthSelection);
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x8));
    if (!(m.flags() & emu::ZF))
        goto branch_8841;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x4));
    if (!(m.flags() & emu::ZF))
        goto branch_8849;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x10));
    if (!(m.flags() & emu::ZF))
        goto branch_885b;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x40));
    if (!(m.flags() & emu::ZF))
        goto branch_8875;
branch_882d:;
    draw_bestiary_category_cursor_core(m);
    dma_sprite_table_core(m);
    dma_palettes_core(m);
    m.call_far(0x4000, 0x0009);
    goto branch_880b;
branch_8841:;
    m.ax() = uint16_t(0x94);
    trigger_sound_effect_core(m);
    goto branch_8893;
branch_8849:;
    m.ax() = uint16_t(0x93);
    trigger_sound_effect_core(m);
    browse_bestiary_list_core(m);
    m.wr16(m.ss(), uint16_t(kCreationRace), uint16_t(0x0));
    goto branch_87f3;
branch_885b:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x1)));
    if (!(m.flags() & emu::CF))
        goto branch_8864;
    m.set_al(uint8_t(uint8_t(m.dx())));
branch_8864:;
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    select_previous_history_member_call_core(m);
    draw_selected_monster_history_call_core(m);
    m.ax() = uint16_t(0x92);
    trigger_sound_effect_core(m);
    goto branch_882d;
branch_8875:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.dx()));
    if (m.flags() & (emu::CF | emu::ZF))
        goto branch_8880;
    m.set_al(uint8_t(0x0));
branch_8880:;
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    select_next_history_member_call_core(m);
    draw_selected_monster_history_call_core(m);
    m.ax() = uint16_t(0x92);
    trigger_sound_effect_core(m);
    goto branch_882d;
branch_8893:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kMenuTransferState))));
    m.out8(uint8_t(0x6), m.al());
    m.flags() &= ~emu::CF;
    return;
}
void run_monster_history_menu(Machine &m) {
    run_monster_history_menu_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x348D57
// Open the inn confirmation menu and restore the field display afterward.
void run_inn_menu_core(Machine &m) {
    UiSceneScope ui_scope(m, hooks::Event::ServiceMenu, 8);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    {
        const uint16_t callback_cs = m.cs();
        save_field_display_for_menu_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    computed::push(m, m.ds());
    m.ax() = uint16_t(0x4000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0xE4);
    m.cx() = uint16_t(0xC);
    m.si() = uint16_t(kMenuBackgroundPalettes);
    copy_tiles_core(m);
    m.ds() = uint16_t(computed::pop(m));
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.es() = uint16_t(m.ax());
    m.wr8(m.ss(), uint16_t(kMenuSelection), uint8_t(0x0));
    draw_inn_confirmation_cursor_call_core(m);
    draw_inn_screen_call_core(m);
    dma_sprite_table_core(m);
    dma_palettes_core(m);
    m.call_far(0x4000, 0x0009);
branch_8d9a:;
    m.call_far(0x4000, 0x005C);
    m.ax() =
        uint16_t(hooks::filter(hooks::Value::ServiceChoiceKeys, m, m.rd16(m.ss(), uint16_t(kKeysRepeat))));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x1));
    m.si() = uint16_t(kMenuSelection);
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x8));
    if (!(m.flags() & emu::ZF))
        goto branch_8dd0;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x4));
    if (!(m.flags() & emu::ZF))
        goto branch_8dd8;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x10));
    if (!(m.flags() & emu::ZF))
        goto branch_8df1;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x40));
    if (!(m.flags() & emu::ZF))
        goto branch_8dfc;
branch_8dbc:;
    draw_inn_confirmation_cursor_call_core(m);
    dma_sprite_table_core(m);
    dma_palettes_core(m);
    m.call_far(0x4000, 0x0009);
    goto branch_8d9a;
branch_8dd0:;
    m.ax() = uint16_t(0x94);
    trigger_sound_effect_core(m);
    goto branch_8e12;
branch_8dd8:;
    m.bx() = uint16_t(
        (m.bx() & 0x00FF) |
        (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx() >> 8) ^ uint8_t(m.bx() >> 8)))))
         << 8));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.bx() = uint16_t(computed::shift_left<uint16_t>(m, m.bx(), 0x1));
    m.ax() = uint16_t(m.rd16(m.cs(), uint16_t(m.bx() + 0x8DED)));
    {
        const uint16_t callback_cs = m.cs();
        dispatch_computed_handler_core(m, m.ax());
        if (m.cs() != callback_cs)
            return;
    }
    if (m.flags() & emu::CF)
        goto branch_8e12;
    draw_inn_screen_call_core(m);
    goto branch_8dbc;
branch_8df1:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x1)));
    if (!(m.flags() & emu::CF))
        goto branch_8e07;
    m.set_al(uint8_t(uint8_t(m.dx())));
    goto branch_8e07;
branch_8dfc:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.dx()));
    if (m.flags() & (emu::CF | emu::ZF))
        goto branch_8e07;
    m.set_al(uint8_t(0x0));
branch_8e07:;
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    m.ax() = uint16_t(0x92);
    trigger_sound_effect_core(m);
    goto branch_8dbc;
branch_8e12:;
    restore_saved_menu_buffers_call_core(m);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void run_inn_menu(Machine &m) {
    run_inn_menu_core(m);
    if (m.cs() == 0x4000)
        m.retf();
}
// rom: 0x348E1A (near)
// Play the cancel sound and return with carry set.
void cancel_inn_stay_core(Machine &m) {
    m.ax() = uint16_t(0x94);
    trigger_sound_effect_core(m);
    m.flags() |= emu::CF;
    return;
}
void cancel_inn_stay(Machine &m) {
    cancel_inn_stay_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x348E22 (near)
// Pay the missing-HP cost, restore living members' HP and refill ability IDs 80h..FBh.
void purchase_inn_stay_core(Machine &m) {
    UiSceneScope ui_scope(m, hooks::Event::ServiceMenu, 8);
    m.ax() = uint16_t(0x93);
    trigger_sound_effect_core(m);
    m.dx() = uint16_t(m.rd16(m.ss(), uint16_t(kGoldHigh)));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kGoldLow)));
    (void)computed::subtract<uint16_t>(m, m.dx(), m.rd16(m.ss(), uint16_t(kMenuPriceHigh)));
    if (!(m.flags() & emu::CF))
        goto branch_8e3b;
    goto branch_8efe;
branch_8e3b:;
    if (!(m.flags() & (emu::CF | emu::ZF)))
        goto branch_8e47;
    (void)computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kMenuPriceLow)));
    if (!(m.flags() & emu::CF))
        goto branch_8e47;
    goto branch_8efe;
// Compare high words first, then low words; subtract the low-word borrow from the high word.
branch_8e47:;
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kMenuPriceLow))));
    m.dx() = uint16_t(computed::subtract<uint16_t>(m, m.dx(), m.rd16(m.ss(), uint16_t(kMenuPriceHigh)),
                                                   bool(m.flags() & emu::CF)));
    m.wr16(m.ss(), uint16_t(kGoldHigh), uint16_t(m.dx()));
    m.wr16(m.ss(), uint16_t(kGoldLow), uint16_t(m.ax()));
    m.cx() = uint16_t(0x4);
    m.si() = uint16_t(kGameState);
branch_8e60:;
    (void)computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ss(), uint16_t(m.si())) & 0x1));
    if (!(m.flags() & emu::ZF))
        goto branch_8e6e;
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.si() + 0x0008)));
    m.wr16(m.ss(), uint16_t(m.si() + 0x0006), uint16_t(m.ax()));
branch_8e6e:;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x1F));
    if (--m.cx())
        goto branch_8e60;
    m.cx() = uint16_t(0x4);
    m.si() = uint16_t(kGameState);
branch_8e79:;
    (void)computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ss(), uint16_t(m.si())) & 0x1));
    if (!(m.flags() & emu::ZF))
        goto branch_8eb5;
    computed::push(m, m.cx());
    computed::push(m, m.si());
    computed::push(m, m.es());
    m.ax() = uint16_t(0x9000);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xF));
    m.cx() = uint16_t(0x8);
// Only ability IDs 80h..FBh refill; physical equipment, empty slots and FCh..FFh are skipped.
branch_8e8d:;
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0x80);
    if (m.flags() & emu::CF)
        goto branch_8ead;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFC);
    if (!(m.flags() & emu::CF))
        goto branch_8ead;
    m.bx() = uint16_t(
        (m.bx() & 0x00FF) |
        (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx() >> 8) ^ uint8_t(m.bx() >> 8)))))
         << 8));
    m.bx() = uint16_t(computed::shift_left<uint16_t>(m, m.bx(), 0x1));
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), 0x41E0));
    m.bx() = uint16_t(m.rd16(m.es(), uint16_t(m.bx())));
    m.set_al(uint8_t(table_at(m, Table::item_records, m.es(), uint16_t(m.bx() + 0x0003))));
    m.wr8(m.ss(), uint16_t(m.si() + 0x0001), uint8_t(m.al()));
branch_8ead:;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    if (--m.cx())
        goto branch_8e8d;
    m.es() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
branch_8eb5:;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x1F));
    if (--m.cx())
        goto branch_8e79;
    m.ax() = uint16_t(kPaletteBuffer);
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(kPaletteTarget);
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x200);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        fade_palettes_16_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    draw_inn_screen_call_core(m);
    m.cx() = uint16_t(0x1E);
branch_8ed8:;
    m.call_far(0x4000, 0x0009);
    if (--m.cx())
        goto branch_8ed8;
    m.set_al(uint8_t(0x89));
    m.wr8(m.ss(), uint16_t(kSfxRequest), uint8_t(m.al()));
    m.cx() = uint16_t(0x12C);
branch_8ee8:;
    m.call_far(0x4000, 0x0009);
    if (--m.cx())
        goto branch_8ee8;
    clear_inn_message_core(m);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        fade_field_actors_to_target_core(m);
        if (m.cs() != 0x5000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0x4));
    wait_menu_error_message_core(m);
    m.flags() |= emu::CF;
    return;
branch_8efe:;
    m.ax() = uint16_t(0x2A);
    trigger_sound_effect_core(m);
    m.set_al(uint8_t(0x1));
    wait_menu_error_message_core(m);
    m.flags() &= ~emu::CF;
    return;
}
void purchase_inn_stay(Machine &m) {
    purchase_inn_stay_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x3490CD (near)
// Save sprite, palette and wrapped screen buffers and reserve menu cursor sprites.
void save_field_display_for_menu_core(Machine &m) {
    const uint16_t field_scroll = uint16_t(m.in8(0x10) | m.in8(0x11) << 8);
    const uint16_t overlay_scroll = uint16_t(m.in8(0x12) | m.in8(0x13) << 8);
    m.ax() = uint16_t(0x0);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(kSpriteBuffer);
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(kSpriteBackup);
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x200);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.ax() = uint16_t(0x0);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(kPaletteBuffer);
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(kPaletteTarget);
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x200);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.ax() = uint16_t(0x100);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x200);
    m.es() = uint16_t(m.ax());
    m.ax() = uint16_t(uint16_t(m.in8(uint8_t(0x10)) | (uint16_t(m.in8(uint8_t(0x10 + 1))) << 8)));
    {
        const uint16_t callback_cs = m.cs();
        copy_wrapped_screen_view_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x180);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x280);
    m.es() = uint16_t(m.ax());
    m.ax() = uint16_t(uint16_t(m.in8(uint8_t(0x12)) | (uint16_t(m.in8(uint8_t(0x12 + 1))) << 8)));
    {
        const uint16_t callback_cs = m.cs();
        copy_wrapped_screen_view_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.call_far(0x4000, 0x0009);
    m.ax() = uint16_t(0x8);
    computed::out_word(m, uint8_t(0x10), m.ax());
    m.ax() = uint16_t(0x8);
    computed::out_word(m, uint8_t(0x12), m.ax());
    restore_menu_screen_one_call_core(m);
    restore_menu_screen_two_call_core(m);
    hooks::emit(hooks::Event::FieldViewRebased, m, {field_scroll, overlay_scroll});
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0x0);
    m.cx() = uint16_t(0x2020);
    m.dx() = uint16_t(0x1FF);
    m.ax() = uint16_t(0x28);
    m.wr16(m.ss(), uint16_t(kKeyRepeatTimer), uint16_t(m.ax()));
    m.set_al(uint8_t(m.in8(uint8_t(0x6))));
    m.wr8(m.ss(), uint16_t(kMenuDescriptionSpriteStart), uint8_t(m.al()));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0xC)));
    m.out8(uint8_t(0x6), m.al());
    m.si() = uint16_t(kSpriteBackup);
    m.di() = uint16_t(kSpriteBuffer);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kMenuDescriptionSpriteStart))));
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.cx() = uint16_t(m.ax());
    m.ax() = uint16_t(0x3);
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x4));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), m.ax()));
branch_9163:;
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.si())));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.si() + 0x0002)));
    m.wr16(m.ss(), uint16_t(m.di() + 0x0002), uint16_t(m.ax()));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x4));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    if (--m.cx())
        goto branch_9163;
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.set_al(uint8_t(0x0));
    m.cx() = uint16_t(0x3);
branch_9180:;
    computed::push(m, m.ax());
    computed::push(m, m.cx());
    m.dx() = uint16_t(0x3FFB);
    m.bx() = uint16_t(0xE0);
    set_sprite_16x16_core(m);
    m.cx() = uint16_t(computed::pop(m));
    m.ax() = uint16_t(computed::pop(m));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), 0x4));
    if (--m.cx())
        goto branch_9180;
    return;
}
void save_field_display_for_menu(Machine &m) {
    save_field_display_for_menu_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x3495C9
// Select a party member for the paid revival service.
void run_revive_member_menu_core(Machine &m) {
    UiSceneScope ui_scope(m, hooks::Event::ServiceMenu, 9);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    {
        const uint16_t callback_cs = m.cs();
        save_field_display_for_menu_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    computed::push(m, m.ds());
    m.ax() = uint16_t(0x4000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0xE4);
    m.cx() = uint16_t(0xC);
    m.si() = uint16_t(kMenuBackgroundPalettes);
    copy_tiles_core(m);
    m.ds() = uint16_t(computed::pop(m));
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.es() = uint16_t(m.ax());
    m.wr8(m.ss(), uint16_t(kMenuSelection), uint8_t(0x0));
    {
        const uint16_t callback_cs = m.cs();
        draw_revive_member_cursor_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        draw_revive_member_screen_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    dma_sprite_table_core(m);
    dma_palettes_core(m);
    m.call_far(0x4000, 0x0009);
branch_960c:;
    m.call_far(0x4000, 0x005C);
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kKeysRepeat)));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x3));
    m.si() = uint16_t(kMenuSelection);
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x8));
    if (!(m.flags() & emu::ZF))
        goto branch_9642;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x4));
    if (!(m.flags() & emu::ZF))
        goto branch_964a;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x10));
    if (!(m.flags() & emu::ZF))
        goto branch_9658;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x40));
    if (!(m.flags() & emu::ZF))
        goto branch_966f;
branch_962e:;
    {
        const uint16_t callback_cs = m.cs();
        draw_revive_member_cursor_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    dma_sprite_table_core(m);
    dma_palettes_core(m);
    m.call_far(0x4000, 0x0009);
    goto branch_960c;
branch_9642:;
    m.ax() = uint16_t(0x94);
    trigger_sound_effect_core(m);
    goto branch_9688;
branch_964a:;
    m.ax() = uint16_t(0x93);
    trigger_sound_effect_core(m);
    {
        const uint16_t callback_cs = m.cs();
        revive_selected_member_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        draw_revive_member_screen_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    goto branch_962e;
branch_9658:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x1)));
    if (!(m.flags() & emu::CF))
        goto branch_9661;
    m.set_al(uint8_t(uint8_t(m.dx())));
branch_9661:;
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    select_previous_party_member_core(m);
    m.ax() = uint16_t(0x92);
    trigger_sound_effect_core(m);
    goto branch_962e;
branch_966f:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.dx()));
    if (m.flags() & (emu::CF | emu::ZF))
        goto branch_967a;
    m.set_al(uint8_t(0x0));
branch_967a:;
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    select_next_party_member_core(m);
    m.ax() = uint16_t(0x92);
    trigger_sound_effect_core(m);
    goto branch_962e;
branch_9688:;
    restore_saved_menu_buffers_call_core(m);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void run_revive_member_menu(Machine &m) {
    run_revive_member_menu_core(m);
    if (m.cs() == 0x4000)
        m.retf();
}
// rom: 0x349690 (near)
// Charge 100 Gil and one life to revive the selected dead member at one HP; reject ineligible choices.
void revive_selected_member_core(Machine &m) {
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.si() = uint16_t(kGameState);
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x1F));
    {
        const uint16_t product = uint16_t(m.al()) * uint8_t(m.dx());
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.ax()));
    (void)computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ss(), uint16_t(m.si())) & 0x1));
    if (m.flags() & emu::ZF)
        goto branch_9702;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x000A))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0xF))));
    if (m.flags() & emu::ZF)
        goto branch_971c;
    m.dx() = uint16_t(m.rd16(m.ss(), uint16_t(kGoldHigh)));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kGoldLow)));
    (void)computed::subtract<uint16_t>(m, m.dx(), 0x0);
    if (m.flags() & emu::CF)
        goto branch_970f;
    if (!(m.flags() & (emu::CF | emu::ZF)))
        goto branch_96c6;
    (void)computed::subtract<uint16_t>(m, m.ax(), 0x64);
    if (m.flags() & emu::CF)
        goto branch_970f;
branch_96c6:;
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), 0x64));
    m.dx() = uint16_t(computed::subtract<uint16_t>(m, m.dx(), 0x0, bool(m.flags() & emu::CF)));
    m.wr16(m.ss(), uint16_t(kGoldHigh), uint16_t(m.dx()));
    m.wr16(m.ss(), uint16_t(kGoldLow), uint16_t(m.ax()));
    m.wr8(m.ss(), uint16_t(m.si()),
          uint8_t(computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ss(), uint16_t(m.si())) & 0xFE))));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.si() + 0x0008)));
    m.ax() = uint16_t(0x1);
    m.wr16(m.ss(), uint16_t(m.si() + 0x0006), uint16_t(m.ax()));
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si() + 0x000A))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0xF))));
    if (hooks::should(hooks::VB::SpendHeart, m, true, {1}))
        m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ds(), uint16_t(m.si() + 0x000A),
          uint8_t(computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ds(), uint16_t(m.si() + 0x000A)) & 0xF0))));
    m.wr8(m.ds(), uint16_t(m.si() + 0x000A),
          uint8_t(computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ds(), uint16_t(m.si() + 0x000A)) | m.al()))));
    m.ax() = uint16_t(0x5D);
    trigger_sound_effect_core(m);
    {
        const uint16_t callback_cs = m.cs();
        draw_revive_party_summaries_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(0x6));
    wait_menu_error_message_core(m);
branch_9700:;
    m.flags() &= ~emu::CF;
    return;
branch_9702:;
    m.ax() = uint16_t(0x2A);
    trigger_sound_effect_core(m);
    m.set_al(uint8_t(0x2));
    wait_menu_error_message_core(m);
    goto branch_9700;
branch_970f:;
    m.ax() = uint16_t(0x2A);
    trigger_sound_effect_core(m);
    m.set_al(uint8_t(0x1));
    wait_menu_error_message_core(m);
    goto branch_9700;
branch_971c:;
    m.ax() = uint16_t(0x2A);
    trigger_sound_effect_core(m);
    m.set_al(uint8_t(0x3));
    wait_menu_error_message_core(m);
    goto branch_9700;
}
void revive_selected_member(Machine &m) {
    revive_selected_member_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x349729 (near)
// Place the revival menu cursor at the selected party row.
void draw_revive_member_cursor_core(Machine &m) {
    m.bx() = uint16_t(
        (m.bx() & 0x00FF) |
        (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx() >> 8) ^ uint8_t(m.bx() >> 8)))))
         << 8));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kMenuSelection))));
    m.bx() = uint16_t(computed::shift_left<uint16_t>(m, m.bx(), 0x1));
    m.ax() = uint16_t(m.rd16(m.cs(), uint16_t(m.bx() + 0x9745)));
    m.bx() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.dx() = uint16_t(0x3FFB);
    set_sprite_16x16_core(m);
    return;
}
void draw_revive_member_cursor(Machine &m) {
    draw_revive_member_cursor_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x34974D (near)
// Draw the revival service windows, instructions, party summaries and gold.
void draw_revive_member_screen_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x1));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x0)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x3)) << 8));
    draw_window_core(m);
    {
        const TextPtr text_ptr = text(m, TextKey::draw_revive_member_screen_1);
        m.ds() = text_ptr.segment;
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t(0x102);
        m.si() = text_ptr.offset;
        draw_text_core(m);
        text_done(m, TextKey::draw_revive_member_screen_1);
    }
    m.ax() = uint16_t(0x0);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x2));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x3)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xB));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    draw_window_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x2));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x3)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xB));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    draw_window_core(m);
    {
        const uint16_t callback_cs = m.cs();
        draw_revive_party_summaries_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x1));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xD)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1C));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x5)) << 8));
    draw_window_core(m);
    {
        const TextPtr text_ptr = text(m, TextKey::draw_revive_member_screen_2);
        m.ds() = text_ptr.segment;
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t(0xE05);
        m.si() = text_ptr.offset;
        draw_text_core(m);
        text_done(m, TextKey::draw_revive_member_screen_2);
    }
    {
        const TextPtr text_ptr = text(m, TextKey::draw_revive_member_screen_3);
        m.ds() = text_ptr.segment;
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t(0xF05);
        m.si() = text_ptr.offset;
        draw_text_core(m);
        text_done(m, TextKey::draw_revive_member_screen_3);
    }
    {
        const TextPtr text_ptr = text(m, TextKey::draw_revive_member_screen_4);
        m.ds() = text_ptr.segment;
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t(0x1005);
        m.si() = text_ptr.offset;
        draw_text_core(m);
        text_done(m, TextKey::draw_revive_member_screen_4);
    }
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x13));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x3)) << 8));
    draw_window_core(m);
    format_gold_core(m);
    {
        const TextPtr text_ptr = text(m, TextKey::draw_revive_member_screen_5);
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x14));
        m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xB)) << 8));
        m.ds() = text_ptr.segment;
        m.si() = text_ptr.offset;
        TextValues values; // {gold} (text_tokens.h): the amount in the string, not drawn over its gap
        values.set(U"gold", ram_text(m, 0, kGoldText));
        draw_text_core(m);
        text_done(m, TextKey::draw_revive_member_screen_5);
    }
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x14));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xB)) << 8));
    m.ax() = uint16_t(0x1);
    m.si() = uint16_t(kGoldText);
    if (!has_value_tokens(m, TextKey::draw_revive_member_screen_5))
        draw_text_core(m);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void draw_revive_member_screen(Machine &m) {
    draw_revive_member_screen_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x349809 (near)
// Draw compact party icons and member summaries for the revival service.
void draw_revive_party_summaries_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    load_compact_party_graphics_call_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0x403);
    m.dx() = uint16_t(0x0);
    draw_compact_member_icon_call_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0x603);
    m.dx() = uint16_t(0x1);
    draw_compact_member_icon_call_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0x803);
    m.dx() = uint16_t(0x2);
    draw_compact_member_icon_call_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0xA03);
    m.dx() = uint16_t(0x3);
    draw_compact_member_icon_call_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0x405);
    m.si() = uint16_t(kGameState);
    draw_inn_member_summary_call_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0x605);
    m.si() = uint16_t(kMember1Record);
    draw_inn_member_summary_call_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0x805);
    m.si() = uint16_t(kMember2Record);
    draw_inn_member_summary_call_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0xA05);
    m.si() = uint16_t(kMember3Record);
    draw_inn_member_summary_call_core(m);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void draw_revive_party_summaries(Machine &m) {
    draw_revive_party_summaries_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x34987A
// Select the party member to replace with the offered character type.
void run_recruitment_member_menu_core(Machine &m) {
    UiSceneScope ui_scope(m, hooks::Event::ServiceMenu, 10);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kCreationRace), uint8_t(m.al()));
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    {
        const uint16_t callback_cs = m.cs();
        save_field_display_for_menu_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    computed::push(m, m.ds());
    m.ax() = uint16_t(0x4000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0xE4);
    m.cx() = uint16_t(0xC);
    m.si() = uint16_t(kMenuBackgroundPalettes);
    copy_tiles_core(m);
    m.ds() = uint16_t(computed::pop(m));
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.es() = uint16_t(m.ax());
    m.wr8(m.ss(), uint16_t(kMenuActionIndex), uint8_t(0x0));
    {
        const uint16_t callback_cs = m.cs();
        draw_recruitment_member_cursor_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        draw_recruitment_member_screen_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    dma_sprite_table_core(m);
    dma_palettes_core(m);
    m.call_far(0x4000, 0x0009);
branch_98c1:;
    m.call_far(0x4000, 0x005C);
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kKeysRepeat)));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x3));
    m.si() = uint16_t(kMenuActionIndex);
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x8));
    if (!(m.flags() & emu::ZF))
        goto branch_98f7;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x4));
    if (!(m.flags() & emu::ZF))
        goto branch_98ff;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x10));
    if (!(m.flags() & emu::ZF))
        goto branch_9911;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x40));
    if (!(m.flags() & emu::ZF))
        goto branch_991c;
branch_98e3:;
    {
        const uint16_t callback_cs = m.cs();
        draw_recruitment_member_cursor_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    dma_sprite_table_core(m);
    dma_palettes_core(m);
    m.call_far(0x4000, 0x0009);
    goto branch_98c1;
branch_98f7:;
    m.ax() = uint16_t(0x94);
    trigger_sound_effect_core(m);
    goto branch_9932;
branch_98ff:;
    m.ax() = uint16_t(0x93);
    trigger_sound_effect_core(m);
    {
        const uint16_t callback_cs = m.cs();
        replace_selected_party_member_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    if (m.flags() & emu::CF)
        goto branch_990f;
    {
        const uint16_t callback_cs = m.cs();
        draw_recruitment_member_screen_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    goto branch_98e3;
branch_990f:;
    goto branch_9932;
branch_9911:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x1)));
    if (!(m.flags() & emu::CF))
        goto branch_9927;
    m.set_al(uint8_t(uint8_t(m.dx())));
    goto branch_9927;
branch_991c:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.dx()));
    if (m.flags() & (emu::CF | emu::ZF))
        goto branch_9927;
    m.set_al(uint8_t(0x0));
branch_9927:;
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    m.ax() = uint16_t(0x92);
    trigger_sound_effect_core(m);
    goto branch_98e3;
branch_9932:;
    restore_saved_menu_buffers_call_core(m);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void run_recruitment_member_menu(Machine &m) {
    run_recruitment_member_menu_core(m);
    if (m.cs() == 0x4000)
        m.retf();
}
// rom: 0x34993A (near)
// Validate the selected member, confirm replacement and run character creation.
void replace_selected_party_member_core(Machine &m) {
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    (void)computed::subtract<uint8_t>(m, m.rd8(m.ss(), uint16_t(kCreationRace)), 0x6);
    if (!(m.flags() & emu::CF))
        goto branch_9954;
    (void)computed::subtract<uint8_t>(m, m.al(), m.rd8(m.ss(), uint16_t(kDialogueLeader)));
    if (!(m.flags() & emu::ZF))
        goto branch_9954;
    goto branch_99d6;
branch_9954:;
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.si() = uint16_t(kGameState);
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x1F));
    {
        const uint16_t product = uint16_t(m.al()) * uint8_t(m.dx());
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.ax()));
    (void)computed::subtract<uint8_t>(m, m.rd8(m.ss(), uint16_t(kCreationRace)), 0x6);
    if (!(m.flags() & emu::CF))
        goto branch_9970;
    (void)computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ss(), uint16_t(m.si())) & 0x1));
    if (!(m.flags() & emu::ZF))
        goto branch_9970;
    goto branch_99f0;
branch_9970:;
    computed::push(m, m.si());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xF));
    m.cx() = uint16_t(0x8);
branch_9977:;
    (void)computed::subtract<uint8_t>(m, m.rd8(m.ss(), uint16_t(m.si())), 0x11);
    if (m.flags() & emu::ZF)
        goto branch_9991;
    (void)computed::subtract<uint8_t>(m, m.rd8(m.ss(), uint16_t(m.si())), 0x12);
    if (m.flags() & emu::ZF)
        goto branch_9991;
    (void)computed::subtract<uint8_t>(m, m.rd8(m.ss(), uint16_t(m.si())), 0x13);
    if (m.flags() & emu::ZF)
        goto branch_9991;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    if (--m.cx())
        goto branch_9977;
    m.si() = uint16_t(computed::pop(m));
    goto branch_9994;
branch_9991:;
    m.si() = uint16_t(computed::pop(m));
    goto branch_99e3;
branch_9994:;
    {
        const uint16_t callback_cs = m.cs();
        confirm_member_replacement_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    if (m.flags() & emu::CF)
        goto branch_99d4;
    {
        // Character creation owns the screen (its own fade, type list and name entry): no Guild picture over
        // it until the native Guild screen is drawn again.
        UiSceneScope creation_scope(m, hooks::Event::ServiceMenu, 0);
        m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
        m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() ^ m.bx())));
        m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kMenuActionIndex))));
        m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kCreationRace))));
        new_game_setup_core(m);
        dma_sprite_table_core(m);
        dma_palettes_core(m);
        m.call_far(0x4000, 0x0009);
        restore_saved_menu_buffers_call_core(m);
        {
            const uint16_t callback_cs = m.cs();
            save_field_display_for_menu_core(m);
            if (m.cs() != callback_cs)
                return;
        }
        {
            const uint16_t callback_cs = m.cs();
            draw_recruitment_member_screen_core(m);
            if (m.cs() != callback_cs)
                return;
        }
        dma_sprite_table_core(m);
        dma_palettes_core(m);
    }
    m.call_far(0x4000, 0x0009);
    m.flags() &= ~emu::CF;
    return;
branch_99d4:;
    m.flags() &= ~emu::CF;
    return;
branch_99d6:;
    m.ax() = uint16_t(0x2A);
    trigger_sound_effect_core(m);
    m.set_al(uint8_t(0x5));
    wait_menu_error_message_core(m);
    goto branch_99d4;
branch_99e3:;
    m.ax() = uint16_t(0x2A);
    trigger_sound_effect_core(m);
    m.set_al(uint8_t(0x9));
    wait_menu_error_message_core(m);
    goto branch_99d4;
branch_99f0:;
    // A living member of a human/esper row cannot be replaced: the notice waits for any key.
    {
        UiSceneScope ui_scope(m, hooks::Event::ServiceMenu, 12);
        const uint16_t callback_cs = m.cs();
        replace_selected_party_member_refusal_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    goto branch_99d4;
}
// The tail of rom 0x34993A from 0x3499F0: draw the refusal beside the selected row, wait for a key, erase it.
void replace_selected_party_member_refusal_core(Machine &m) {
    m.dx() =
        uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(m.rd8(m.ss(), uint16_t(kMenuActionIndex)))) << 8));
    m.dx() = uint16_t((m.dx() & 0x00FF) |
                      (uint16_t(uint8_t(computed::shift_left<uint8_t>(m, uint8_t(m.dx() >> 8), 0x1))) << 8));
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x9));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x4)) << 8));
    m.bx() = uint16_t(
        (m.bx() & 0x00FF) |
        (uint16_t(uint8_t(computed::add<uint8_t>(m, uint8_t(m.bx() >> 8), uint8_t(m.dx() >> 8)))) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xB));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x3)) << 8));
    draw_window_core(m);
    {
        const TextPtr text_ptr = text(m, TextKey::replace_selected_party_member_1);
        m.ds() = text_ptr.segment;
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t(0x50A);
        m.bx() = uint16_t(
            (m.bx() & 0x00FF) |
            (uint16_t(uint8_t(computed::add<uint8_t>(m, uint8_t(m.bx() >> 8), uint8_t(m.dx() >> 8)))) << 8));
        m.si() = text_ptr.offset;
        draw_text_core(m);
        text_done(m, TextKey::replace_selected_party_member_1);
    }
    m.ax() = uint16_t(0x2A);
    trigger_sound_effect_core(m);
branch_9a21:;
    m.call_far(0x4000, 0x0009);
    m.call_far(0x4000, 0x005C);
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kKeysPressed)));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | m.ax())));
    if (m.flags() & emu::ZF)
        goto branch_9a21;
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x9));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x4)) << 8));
    m.bx() = uint16_t(
        (m.bx() & 0x00FF) |
        (uint16_t(uint8_t(computed::add<uint8_t>(m, uint8_t(m.bx() >> 8), uint8_t(m.dx() >> 8)))) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xB));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x3)) << 8));
    m.si() = uint16_t(kMenuScreenBackup);
    m.di() = uint16_t(kScreenMap2);
    copy_screen_rectangle_core(m);
}
void replace_selected_party_member(Machine &m) {
    replace_selected_party_member_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x349A48 (near)
// Ask whether to replace the selected member and return the answer in carry.
void confirm_member_replacement_core(Machine &m) {
    UiSceneScope ui_scope(m, hooks::Event::ServiceMenu, 11);
    m.set_al(uint8_t(0x1));
    {
        const uint16_t callback_cs = m.cs();
        show_shop_submenu_cursor_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.wr8(m.ss(), uint16_t(kMenuSelection), uint8_t(0x0));
    {
        const uint16_t callback_cs = m.cs();
        draw_replacement_confirmation_cursor_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        draw_member_replacement_confirmation_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    dma_sprite_table_core(m);
    dma_palettes_core(m);
    m.call_far(0x4000, 0x0009);
    computed::push(m, m.ds());
    {
        computed::push(m, m.cs());
        m.ds() = uint16_t(computed::pop(m));
        const TextPtr text_ptr = text(m, TextKey::confirm_member_replacement_1);
        m.ds() = text_ptr.segment;
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t(0xE05);
        m.si() = text_ptr.offset;
        draw_text_core(m);
        text_done(m, TextKey::confirm_member_replacement_1);
    }
    m.ds() = uint16_t(computed::pop(m));
branch_9a7a:;
    m.call_far(0x4000, 0x005C);
    m.ax() =
        uint16_t(hooks::filter(hooks::Value::ServiceChoiceKeys, m, m.rd16(m.ss(), uint16_t(kKeysRepeat))));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x1));
    m.si() = uint16_t(kMenuSelection);
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x8));
    if (!(m.flags() & emu::ZF))
        goto branch_9ab0;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x4));
    if (!(m.flags() & emu::ZF))
        goto branch_9ab9;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x10));
    if (!(m.flags() & emu::ZF))
        goto branch_9aca;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x40));
    if (!(m.flags() & emu::ZF))
        goto branch_9ad5;
branch_9a9c:;
    {
        const uint16_t callback_cs = m.cs();
        draw_replacement_confirmation_cursor_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    dma_sprite_table_core(m);
    dma_palettes_core(m);
    m.call_far(0x4000, 0x0009);
    goto branch_9a7a;
branch_9ab0:;
    m.ax() = uint16_t(0x94);
    trigger_sound_effect_core(m);
    m.flags() |= emu::CF;
    goto branch_9aeb;
branch_9ab9:;
    (void)computed::subtract<uint8_t>(m, m.rd8(m.ss(), uint16_t(kMenuSelection)), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_9ab0;
    m.ax() = uint16_t(0x93);
    trigger_sound_effect_core(m);
    m.flags() &= ~emu::CF;
    goto branch_9aeb;
branch_9aca:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x1)));
    if (!(m.flags() & emu::CF))
        goto branch_9ae0;
    m.set_al(uint8_t(uint8_t(m.dx())));
    goto branch_9ae0;
branch_9ad5:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.dx()));
    if (m.flags() & (emu::CF | emu::ZF))
        goto branch_9ae0;
    m.set_al(uint8_t(0x0));
branch_9ae0:;
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    m.ax() = uint16_t(0x92);
    trigger_sound_effect_core(m);
    goto branch_9a9c;
branch_9aeb:;
    computed::push(m, uint16_t(m.flags() | 0xF002));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x17));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x9)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x6));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x4)) << 8));
    m.si() = uint16_t(kMenuScreenBackup);
    m.di() = uint16_t(kScreenMap2);
    {
        const uint16_t callback_cs = m.cs();
        copy_screen_rectangle_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(0x1));
    {
        const uint16_t callback_cs = m.cs();
        hide_shop_submenu_cursor_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.flags() = uint16_t(computed::pop(m) & 0x0FD5);
    return;
}
void confirm_member_replacement(Machine &m) {
    confirm_member_replacement_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x349B04 (near)
// Place the recruitment menu cursor at the selected party row.
void draw_recruitment_member_cursor_core(Machine &m) {
    m.bx() = uint16_t(
        (m.bx() & 0x00FF) |
        (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx() >> 8) ^ uint8_t(m.bx() >> 8)))))
         << 8));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kMenuActionIndex))));
    m.bx() = uint16_t(computed::shift_left<uint16_t>(m, m.bx(), 0x1));
    m.ax() = uint16_t(m.rd16(m.cs(), uint16_t(m.bx() + 0x9B24)));
    m.wr16(m.ss(), uint16_t(kMenuCursorPosition), uint16_t(m.ax()));
    m.bx() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.dx() = uint16_t(0x3FFB);
    set_sprite_16x16_core(m);
    return;
}
void draw_recruitment_member_cursor(Machine &m) {
    draw_recruitment_member_cursor_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x349B2C (near)
// Place the yes/no cursor for party-member replacement.
void draw_replacement_confirmation_cursor_core(Machine &m) {
    m.bx() = uint16_t(
        (m.bx() & 0x00FF) |
        (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx() >> 8) ^ uint8_t(m.bx() >> 8)))))
         << 8));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kMenuSelection))));
    m.bx() = uint16_t(computed::shift_left<uint16_t>(m, m.bx(), 0x1));
    m.ax() = uint16_t(m.rd16(m.cs(), uint16_t(m.bx() + 0x9B4C)));
    m.wr16(m.ss(), uint16_t(kMenuCursorPosition), uint16_t(m.ax()));
    m.bx() = uint16_t(m.ax());
    m.ax() = uint16_t(0x0);
    m.dx() = uint16_t(0x3FFB);
    set_sprite_16x16_core(m);
    return;
}
void draw_replacement_confirmation_cursor(Machine &m) {
    draw_replacement_confirmation_cursor_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x349B50 (near)
// Draw the party-member replacement windows and instructions.
void draw_recruitment_member_screen_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x1));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x0)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xE));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x3)) << 8));
    draw_window_core(m);
    {
        const TextPtr text_ptr = text(m, TextKey::draw_recruitment_member_screen_1);
        m.ds() = text_ptr.segment;
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t(0x102);
        m.si() = text_ptr.offset;
        draw_text_core(m);
        text_done(m, TextKey::draw_recruitment_member_screen_1);
    }
    m.ax() = uint16_t(0x0);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x2));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x3)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xB));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    draw_window_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x2));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x3)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xB));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    draw_window_core(m);
    {
        const uint16_t callback_cs = m.cs();
        draw_recruitment_party_summaries_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x1));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xD)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1C));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x5)) << 8));
    draw_window_core(m);
    {
        const TextPtr text_ptr = text(m, TextKey::draw_recruitment_member_screen_2);
        m.ds() = text_ptr.segment;
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t(0xE05);
        m.si() = text_ptr.offset;
        draw_text_core(m);
        text_done(m, TextKey::draw_recruitment_member_screen_2);
    }
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void draw_recruitment_member_screen(Machine &m) {
    draw_recruitment_member_screen_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x349BBA (near)
// Draw the yes/no confirmation window for member replacement.
void draw_member_replacement_confirmation_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x17));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x9)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x6));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x4)) << 8));
    draw_window_core(m);
    {
        const TextPtr text_ptr = text(m, TextKey::draw_member_replacement_confirmation_1);
        m.ds() = text_ptr.segment;
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t(0xA18);
        m.si() = text_ptr.offset;
        draw_text_core(m);
        text_done(m, TextKey::draw_member_replacement_confirmation_1);
    }
    {
        const TextPtr text_ptr = text(m, TextKey::draw_member_replacement_confirmation_2);
        m.ds() = text_ptr.segment;
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t(0xB18);
        m.si() = text_ptr.offset;
        draw_text_core(m);
        text_done(m, TextKey::draw_member_replacement_confirmation_2);
    }
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void draw_member_replacement_confirmation(Machine &m) {
    draw_member_replacement_confirmation_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x349BF1 (near)
// Draw compact party icons and member summaries for recruitment.
void draw_recruitment_party_summaries_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    load_compact_party_graphics_call_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0x403);
    m.dx() = uint16_t(0x0);
    draw_compact_member_icon_call_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0x603);
    m.dx() = uint16_t(0x1);
    draw_compact_member_icon_call_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0x803);
    m.dx() = uint16_t(0x2);
    draw_compact_member_icon_call_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0xA03);
    m.dx() = uint16_t(0x3);
    draw_compact_member_icon_call_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0x405);
    m.si() = uint16_t(kGameState);
    draw_inn_member_summary_call_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0x605);
    m.si() = uint16_t(kMember1Record);
    draw_inn_member_summary_call_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0x805);
    m.si() = uint16_t(kMember2Record);
    draw_inn_member_summary_call_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0xA05);
    m.si() = uint16_t(kMember3Record);
    draw_inn_member_summary_call_core(m);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void draw_recruitment_party_summaries(Machine &m) {
    draw_recruitment_party_summaries_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x349C62
// Open AL's shop and dispatch buy, sell and exit commands.
void run_shop_menu_core(Machine &m) {
    UiSceneScope ui_scope(m, hooks::Event::ServiceMenu, 1);
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.wr8(m.ss(), uint16_t(kCreationLeader), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.rd8(m.ss(), uint16_t(kCreationLeader)), 0x16);
    if (!(m.flags() & emu::ZF))
        goto branch_9c80;
    m.wr16(m.ss(), uint16_t(kGoldHigh), uint16_t(0xF));
    m.wr16(m.ss(), uint16_t(kGoldLow), uint16_t(0x423F));
branch_9c80:;
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    {
        const uint16_t callback_cs = m.cs();
        save_field_display_for_menu_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        draw_empty_shop_description_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x9000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0x20);
    m.si() = uint16_t(0x2480);
    copy_tiles_core(m);
    m.ax() = uint16_t(0xA0);
    m.cx() = uint16_t(0x2C);
    m.si() = uint16_t(0x2AC0);
    copy_tiles_core(m);
    computed::push(m, m.ds());
    computed::push(m, m.cs());
    m.ds() = uint16_t(computed::pop(m));
    m.ax() = uint16_t(0x1F5);
    m.cx() = uint16_t(0x2);
    m.si() = uint16_t(kShopMenuPalettes);
    copy_tiles_core(m);
    m.ds() = uint16_t(computed::pop(m));
    computed::push(m, m.ds());
    m.ax() = uint16_t(0x4000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0xE4);
    m.cx() = uint16_t(0xC);
    m.si() = uint16_t(kMenuBackgroundPalettes);
    copy_tiles_core(m);
    m.ds() = uint16_t(computed::pop(m));
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.es() = uint16_t(m.ax());
    m.wr8(m.ss(), uint16_t(kMenuSelection), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kSaveSlotSelection), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kSaveChoice), uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(kMenuThirdSelection), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kMenuFourthSelection), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kMenuFifthSelection), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kCreationRace), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kNamePage), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kNameInsertionIndex), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kNameText), uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kShopCursorBlink), uint8_t(0x0));
    m.si() = uint16_t(kShopStock);
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kCreationLeader))));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0xA));
    {
        const uint16_t product = uint16_t(m.al()) * uint8_t(m.dx());
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.ax()));
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0xA);
branch_9d31:;
    (void)computed::subtract<uint8_t>(m, table_at(m, Table::shops, m.cs(), m.si()), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_9d3b;
    m.ax() = uint16_t(computed::increment<uint16_t>(m, m.ax()));
    m.si() = uint16_t(computed::increment<uint16_t>(m, m.si()));
    if (--m.cx())
        goto branch_9d31;
branch_9d3b:;
    (void)computed::subtract<uint8_t>(m, m.rd8(m.ss(), uint16_t(kCreationLeader)), 0x16);
    if (!(m.flags() & emu::ZF))
        goto branch_9d45;
    m.set_al(uint8_t(0xFC));
branch_9d45:;
    m.wr8(m.ss(), uint16_t(kNamePage), uint8_t(m.al()));
    {
        const uint16_t callback_cs = m.cs();
        draw_shop_command_cursor_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        draw_shop_main_screen_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    dma_sprite_table_core(m);
    dma_palettes_core(m);
    m.set_al(uint8_t(0x0));
    {
        const uint16_t callback_cs = m.cs();
        blink_shop_submenu_cursors_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.call_far(0x4000, 0x0009);
branch_9d63:;
    m.call_far(0x4000, 0x005C);
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kKeysRepeat)));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x2));
    m.si() = uint16_t(kMenuSelection);
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x8));
    if (!(m.flags() & emu::ZF))
        goto branch_9d9e;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x4));
    if (!(m.flags() & emu::ZF))
        goto branch_9da6;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x10));
    if (!(m.flags() & emu::ZF))
        goto branch_9dc4;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x40));
    if (!(m.flags() & emu::ZF))
        goto branch_9dcf;
branch_9d85:;
    {
        const uint16_t callback_cs = m.cs();
        draw_shop_command_cursor_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    dma_sprite_table_core(m);
    dma_palettes_core(m);
    m.set_al(uint8_t(0x0));
    {
        const uint16_t callback_cs = m.cs();
        blink_shop_submenu_cursors_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.call_far(0x4000, 0x0009);
    goto branch_9d63;
branch_9d9e:;
    m.ax() = uint16_t(0x94);
    trigger_sound_effect_core(m);
    goto branch_9de5;
branch_9da6:;
    m.bx() = uint16_t(
        (m.bx() & 0x00FF) |
        (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx() >> 8) ^ uint8_t(m.bx() >> 8)))))
         << 8));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.bx() = uint16_t(computed::shift_left<uint16_t>(m, m.bx(), 0x1));
    m.ax() = uint16_t(m.rd16(m.cs(), uint16_t(m.bx() + 0x9DBE)));
    {
        const uint16_t callback_cs = m.cs();
        dispatch_computed_handler_core(m, m.ax());
        if (m.cs() != callback_cs)
            return;
    }
    if (m.flags() & emu::CF)
        goto branch_9de5;
    {
        const uint16_t callback_cs = m.cs();
        draw_empty_shop_description_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        draw_shop_main_screen_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    goto branch_9d85;
branch_9dc4:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x1)));
    if (!(m.flags() & emu::CF))
        goto branch_9dda;
    m.set_al(uint8_t(uint8_t(m.dx())));
    goto branch_9dda;
branch_9dcf:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.dx()));
    if (m.flags() & (emu::CF | emu::ZF))
        goto branch_9dda;
    m.set_al(uint8_t(0x0));
branch_9dda:;
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    m.ax() = uint16_t(0x92);
    trigger_sound_effect_core(m);
    goto branch_9d85;
branch_9de5:;
    restore_saved_menu_buffers_call_core(m);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void run_shop_menu(Machine &m) {
    run_shop_menu_core(m);
    if (m.cs() == 0x4000)
        m.retf();
}
// rom: 0x349DF5 (near)
// Select an inventory item to sell and display its description.
void run_shop_sell_menu_core(Machine &m) {
    UiSceneScope ui_scope(m, hooks::Event::ServiceMenu, 3);
    m.ax() = uint16_t(0x93);
    trigger_sound_effect_core(m);
    {
        const uint16_t callback_cs = m.cs();
        open_shop_description_row_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(0x1));
    {
        const uint16_t callback_cs = m.cs();
        show_shop_submenu_cursor_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_9e03:;
    m.ax() = uint16_t(0x0);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x1));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xD)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1C));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x5)) << 8));
    draw_window_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x1));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xD)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1C));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x5)) << 8));
    draw_window_core(m);
    {
        const TextPtr text_ptr = text(m, TextKey::run_shop_sell_menu_1);
        m.ds() = text_ptr.segment;
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t(0xE05);
        m.si() = text_ptr.offset;
        draw_text_core(m);
        text_done(m, TextKey::run_shop_sell_menu_1);
    }
    {
        const uint16_t callback_cs = m.cs();
        draw_shop_sale_inventory_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        draw_shop_sell_item_cursor_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    dma_sprite_table_core(m);
    dma_palettes_core(m);
    m.set_al(uint8_t(0x1));
    {
        const uint16_t callback_cs = m.cs();
        blink_shop_submenu_cursors_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.call_far(0x4000, 0x0009);
branch_9e4d:;
    m.call_far(0x4000, 0x005C);
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kKeysRepeat)));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x7));
    m.si() = uint16_t(kMenuFourthSelection);
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x8));
    if (!(m.flags() & emu::ZF))
        goto branch_9e9a;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x4));
    if (!(m.flags() & emu::ZF))
        goto branch_9ec4;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x10));
    if (!(m.flags() & emu::ZF))
        goto branch_9ea3;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x40));
    if (!(m.flags() & emu::ZF))
        goto branch_9eae;
branch_9e6f:;
    {
        const uint16_t callback_cs = m.cs();
        draw_shop_sell_item_cursor_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.si() = uint16_t(kMenuFourthSelection);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x1));
    m.si() = uint16_t(kInventory);
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.ax()));
    {
        const uint16_t callback_cs = m.cs();
        draw_shop_description_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    dma_sprite_table_core(m);
    dma_palettes_core(m);
    m.set_al(uint8_t(0x1));
    {
        const uint16_t callback_cs = m.cs();
        blink_shop_submenu_cursors_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.call_far(0x4000, 0x0009);
    goto branch_9e4d;
branch_9e9a:;
    m.ax() = uint16_t(0x94);
    trigger_sound_effect_core(m);
    goto branch_9f2f;
branch_9ea3:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x1)));
    if (!(m.flags() & emu::CF))
        goto branch_9eb9;
    m.set_al(uint8_t(uint8_t(m.dx())));
    goto branch_9eb9;
branch_9eae:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.dx()));
    if (m.flags() & (emu::CF | emu::ZF))
        goto branch_9eb9;
    m.set_al(uint8_t(0x0));
branch_9eb9:;
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    m.ax() = uint16_t(0x92);
    trigger_sound_effect_core(m);
    goto branch_9e6f;
branch_9ec4:;
    m.ax() = uint16_t(0x93);
    trigger_sound_effect_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x1));
    m.si() = uint16_t(kInventory);
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.ax()));
    (void)computed::subtract<uint8_t>(m, m.rd8(m.ss(), uint16_t(m.si() + 0x0001)), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_9ee6;
    {
        const uint16_t callback_cs = m.cs();
        confirm_shop_sale_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    if (m.flags() & emu::CF)
        goto branch_9f2f;
    goto branch_9e03;
branch_9ee6:;
    computed::push(m, m.ds());
    {
        computed::push(m, m.cs());
        m.ds() = uint16_t(computed::pop(m));
        const TextPtr text_ptr = text(m, TextKey::run_shop_sell_menu_2);
        m.ds() = text_ptr.segment;
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t(0xE05);
        m.si() = text_ptr.offset;
        draw_text_core(m);
        text_done(m, TextKey::run_shop_sell_menu_2);
    }
    m.ds() = uint16_t(computed::pop(m));
    m.ax() = uint16_t(0x2A);
    trigger_sound_effect_core(m);
branch_9efe:;
    m.set_al(uint8_t(0x1));
    {
        const uint16_t callback_cs = m.cs();
        blink_shop_submenu_cursors_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    dma_sprite_table_core(m);
    m.call_far(0x4000, 0x0009);
    m.call_far(0x4000, 0x005C);
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kKeysPressed)));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | m.ax())));
    if (m.flags() & emu::ZF)
        goto branch_9efe;
    computed::push(m, m.ds());
    {
        computed::push(m, m.cs());
        m.ds() = uint16_t(computed::pop(m));
        const TextPtr text_ptr = text(m, TextKey::run_shop_sell_menu_3);
        m.ds() = text_ptr.segment;
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t(0xE05);
        m.si() = text_ptr.offset;
        draw_text_core(m);
        text_done(m, TextKey::run_shop_sell_menu_3);
    }
    m.ds() = uint16_t(computed::pop(m));
    goto branch_9e6f;
branch_9f2f:;
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x2));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x3)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xD));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    m.si() = uint16_t(kMenuScreenBackup);
    m.di() = uint16_t(kScreenMap2);
    {
        const uint16_t callback_cs = m.cs();
        copy_screen_rectangle_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(0x1));
    {
        const uint16_t callback_cs = m.cs();
        hide_shop_submenu_cursor_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        close_shop_description_row_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.flags() &= ~emu::CF;
    return;
}
void run_shop_sell_menu(Machine &m) {
    run_shop_sell_menu_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x349F4A (near)
// Sell the chosen inventory slot, capping total Gil at 999,999, or leave it unchanged on cancellation.
void confirm_shop_sale_core(Machine &m) {
    UiSceneScope ui_scope(m, hooks::Event::ServiceMenu, 5);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.es() = uint16_t(m.ax());
    m.set_al(uint8_t(0x2));
    {
        const uint16_t callback_cs = m.cs();
        show_shop_submenu_cursor_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        draw_shop_secondary_choice_cursor_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        draw_shop_sale_confirmation_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    dma_sprite_table_core(m);
    dma_palettes_core(m);
    m.set_al(uint8_t(0x2));
    {
        const uint16_t callback_cs = m.cs();
        blink_shop_submenu_cursors_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.call_far(0x4000, 0x0009);
branch_9f70:;
    m.call_far(0x4000, 0x005C);
    m.ax() =
        uint16_t(hooks::filter(hooks::Value::ServiceChoiceKeys, m, m.rd16(m.ss(), uint16_t(kKeysRepeat))));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x1));
    m.si() = uint16_t(kMenuFifthSelection);
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x8));
    if (!(m.flags() & emu::ZF))
        goto branch_9fb1;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x4));
    if (!(m.flags() & emu::ZF))
        goto branch_9fba;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x10));
    if (m.flags() & emu::ZF)
        goto branch_9f90;
    goto branch_a01e;
branch_9f90:;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x40));
    if (m.flags() & emu::ZF)
        goto branch_9f98;
    goto branch_a029;
branch_9f98:;
    {
        const uint16_t callback_cs = m.cs();
        draw_shop_secondary_choice_cursor_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    dma_sprite_table_core(m);
    dma_palettes_core(m);
    m.set_al(uint8_t(0x2));
    {
        const uint16_t callback_cs = m.cs();
        blink_shop_submenu_cursors_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.call_far(0x4000, 0x0009);
    goto branch_9f70;
branch_9fb1:;
    m.ax() = uint16_t(0x94);
    trigger_sound_effect_core(m);
    goto branch_a040;
branch_9fba:;
    (void)computed::subtract<uint8_t>(m, m.rd8(m.ss(), uint16_t(kMenuFifthSelection)), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_9fca;
    m.ax() = uint16_t(0x94);
    trigger_sound_effect_core(m);
    goto branch_a040;
branch_9fca:;
    m.dx() = uint16_t(m.rd16(m.ss(), uint16_t(kGoldHigh)));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kGoldLow)));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kMenuPriceLow))));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.rd16(m.ss(), uint16_t(kMenuPriceHigh)),
                                              bool(m.flags() & emu::CF)));
    m.wr16(m.ss(), uint16_t(kGoldHigh), uint16_t(m.dx()));
    m.wr16(m.ss(), uint16_t(kGoldLow), uint16_t(m.ax()));
    (void)computed::subtract<uint16_t>(m, m.dx(), 0xF);
    if (m.flags() & emu::CF)
        goto branch_a000;
    if (!(m.flags() & (emu::CF | emu::ZF)))
        goto branch_9ff2;
    (void)computed::subtract<uint16_t>(m, m.ax(), 0x423F);
    if (m.flags() & (emu::CF | emu::ZF))
        goto branch_a000;
// Saturate the sale proceeds at 0x000F423F = 999,999 Gil.
branch_9ff2:;
    m.wr16(m.ss(), uint16_t(kGoldHigh), uint16_t(0xF));
    m.wr16(m.ss(), uint16_t(kGoldLow), uint16_t(0x423F));
branch_a000:;
    m.si() = uint16_t(kInventory);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kMenuFourthSelection))));
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x1));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.ax()));
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(0xFF));
    m.wr8(m.ss(), uint16_t(m.si() + 0x0001), uint8_t(0xFF));
    m.ax() = uint16_t(0xA1);
    trigger_sound_effect_core(m);
    goto branch_a040;
branch_a01e:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x1)));
    if (!(m.flags() & emu::CF))
        goto branch_a034;
    m.set_al(uint8_t(uint8_t(m.dx())));
    goto branch_a034;
branch_a029:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.dx()));
    if (m.flags() & (emu::CF | emu::ZF))
        goto branch_a034;
    m.set_al(uint8_t(0x0));
branch_a034:;
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    m.ax() = uint16_t(0x92);
    trigger_sound_effect_core(m);
    goto branch_9f98;
branch_a040:;
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x14));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x6)) << 8));
    m.cx() = uint16_t(0x405);
    m.si() = uint16_t(kMenuScreenBackup);
    m.di() = uint16_t(kScreenMap2);
    {
        const uint16_t callback_cs = m.cs();
        copy_screen_rectangle_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(0x2));
    {
        const uint16_t callback_cs = m.cs();
        hide_shop_submenu_cursor_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.flags() &= ~emu::CF;
    return;
}
void confirm_shop_sale(Machine &m) {
    confirm_shop_sale_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x34A057 (near)
// Browse the shop's item list and dispatch purchase confirmation.
void run_shop_buy_menu_core(Machine &m) {
    UiSceneScope ui_scope(m, hooks::Event::ServiceMenu, 2);
    m.ax() = uint16_t(0x93);
    trigger_sound_effect_core(m);
    {
        const uint16_t callback_cs = m.cs();
        open_shop_description_row_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(0x1));
    {
        const uint16_t callback_cs = m.cs();
        show_shop_submenu_cursor_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_a065:;
    m.ax() = uint16_t(0x0);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x1));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xD)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1C));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x5)) << 8));
    draw_window_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x1));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xD)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1C));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x5)) << 8));
    draw_window_core(m);
    {
        const uint16_t callback_cs = m.cs();
        draw_shop_stock_page_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        draw_shop_stock_cursor_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.si() = uint16_t(kNameInsertionIndex);
    {
        const uint16_t callback_cs = m.cs();
        draw_shop_description_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    dma_sprite_table_core(m);
    dma_palettes_core(m);
    m.set_al(uint8_t(0x1));
    {
        const uint16_t callback_cs = m.cs();
        blink_shop_submenu_cursors_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.call_far(0x4000, 0x0009);
branch_a0a5:;
    m.call_far(0x4000, 0x005C);
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kKeysRepeat)));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x2));
    m.si() = uint16_t(kSaveSlotSelection);
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x8));
    if (!(m.flags() & emu::ZF))
        goto branch_a0e0;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x4));
    if (!(m.flags() & emu::ZF))
        goto branch_a135;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x10));
    if (!(m.flags() & emu::ZF))
        goto branch_a0e8;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x40));
    if (!(m.flags() & emu::ZF))
        goto branch_a102;
branch_a0c7:;
    {
        const uint16_t callback_cs = m.cs();
        draw_shop_stock_cursor_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    dma_sprite_table_core(m);
    dma_palettes_core(m);
    m.set_al(uint8_t(0x1));
    {
        const uint16_t callback_cs = m.cs();
        blink_shop_submenu_cursors_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.call_far(0x4000, 0x0009);
    goto branch_a0a5;
branch_a0e0:;
    m.ax() = uint16_t(0x94);
    trigger_sound_effect_core(m);
    goto branch_a144;
branch_a0e8:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_a0fe;
    (void)computed::subtract<uint8_t>(m, m.rd8(m.ss(), uint16_t(kCreationRace)), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_a121;
    m.wr8(m.ss(), uint16_t(kCreationRace),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kCreationRace)), true)));
    goto branch_a121;
branch_a0fe:;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    goto branch_a121;
branch_a102:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (!(m.flags() & emu::ZF))
        goto branch_a11f;
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kCreationRace))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::add<uint8_t>(m, uint8_t(m.bx()), 0x3)));
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), m.rd8(m.ss(), uint16_t(kNamePage)));
    if (m.flags() & emu::ZF)
        goto branch_a121;
    m.wr8(m.ss(), uint16_t(kCreationRace),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kCreationRace)))));
    goto branch_a121;
branch_a11f:;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
branch_a121:;
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    {
        const uint16_t callback_cs = m.cs();
        draw_shop_stock_page_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.si() = uint16_t(kNameInsertionIndex);
    {
        const uint16_t callback_cs = m.cs();
        draw_shop_description_sprites_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0x92);
    trigger_sound_effect_core(m);
    goto branch_a0c7;
branch_a135:;
    m.ax() = uint16_t(0x93);
    trigger_sound_effect_core(m);
    {
        const uint16_t callback_cs = m.cs();
        confirm_shop_purchase_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    if (m.flags() & emu::CF)
        goto branch_a144;
    goto branch_a065;
branch_a144:;
    m.set_al(uint8_t(0x1));
    {
        const uint16_t callback_cs = m.cs();
        hide_shop_submenu_cursor_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        close_shop_description_row_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.flags() &= ~emu::CF;
    return;
}
void run_shop_buy_menu(Machine &m) {
    run_shop_buy_menu_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x34A14E (near)
// Check the 32-bit Gil balance, prepare the subtraction and ask where to put the purchased item.
void confirm_shop_purchase_core(Machine &m) {
    UiSceneScope ui_scope(m, hooks::Event::ServiceMenu, 4);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.es() = uint16_t(m.ax());
    m.dx() = uint16_t(m.rd16(m.ss(), uint16_t(kGoldHigh)));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kGoldLow)));
    (void)computed::subtract<uint16_t>(m, m.dx(), m.rd16(m.ss(), uint16_t(kMenuPriceHigh)));
    if (m.flags() & emu::CF)
        goto branch_a17a;
    if (!(m.flags() & (emu::CF | emu::ZF)))
        goto branch_a16e;
    (void)computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kMenuPriceLow)));
    if (m.flags() & emu::CF)
        goto branch_a17a;
branch_a16e:;
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kMenuPriceLow))));
    m.dx() = uint16_t(computed::subtract<uint16_t>(m, m.dx(), m.rd16(m.ss(), uint16_t(kMenuPriceHigh)),
                                                   bool(m.flags() & emu::CF)));
    goto branch_a1e1;
branch_a17a:;
    m.ax() = uint16_t(0x0);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x1));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xD)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1C));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x5)) << 8));
    draw_window_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x1));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xD)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1C));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x5)) << 8));
    draw_window_core(m);
    {
        hooks::emit(hooks::Event::ServiceMenu, m, {13, 0, int32_t(TextKey::confirm_shop_purchase_1)});
        const TextPtr text_ptr = text(m, TextKey::confirm_shop_purchase_1);
        m.ds() = text_ptr.segment;
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t(0xE05);
        m.si() = text_ptr.offset;
        draw_text_core(m);
        text_done(m, TextKey::confirm_shop_purchase_1);
    }
    m.ax() = uint16_t(0x0);
    m.bx() = uint16_t(0xE0);
    m.dx() = uint16_t(0x3FFB);
    set_sprite_16x16_core(m);
    dma_sprite_table_core(m);
    m.ax() = uint16_t(0x2A);
    trigger_sound_effect_core(m);
branch_a1c3:;
    m.set_al(uint8_t(0x1));
    {
        const uint16_t callback_cs = m.cs();
        blink_shop_submenu_cursors_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    dma_sprite_table_core(m);
    m.call_far(0x4000, 0x0009);
    m.call_far(0x4000, 0x005C);
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kKeysPressed)));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | m.ax())));
    if (m.flags() & emu::ZF)
        goto branch_a1c3;
    m.flags() &= ~emu::CF;
    return;
branch_a1e1:;
    {
        const uint16_t callback_cs = m.cs();
        draw_shop_purchase_cursor_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        draw_shop_purchase_confirmation_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    dma_sprite_table_core(m);
    dma_palettes_core(m);
    m.set_al(uint8_t(0x1));
    {
        const uint16_t callback_cs = m.cs();
        blink_shop_submenu_cursors_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.call_far(0x4000, 0x0009);
branch_a1fb:;
    m.call_far(0x4000, 0x005C);
    m.ax() =
        uint16_t(hooks::filter(hooks::Value::ServiceChoiceKeys, m, m.rd16(m.ss(), uint16_t(kKeysRepeat))));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x1));
    m.si() = uint16_t(kSaveChoice);
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x8));
    if (!(m.flags() & emu::ZF))
        goto branch_a236;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x4));
    if (!(m.flags() & emu::ZF))
        goto branch_a23e;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x10));
    if (!(m.flags() & emu::ZF))
        goto branch_a257;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x40));
    if (!(m.flags() & emu::ZF))
        goto branch_a262;
branch_a21d:;
    {
        const uint16_t callback_cs = m.cs();
        draw_shop_purchase_cursor_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    dma_sprite_table_core(m);
    dma_palettes_core(m);
    m.set_al(uint8_t(0x1));
    {
        const uint16_t callback_cs = m.cs();
        blink_shop_submenu_cursors_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.call_far(0x4000, 0x0009);
    goto branch_a1fb;
branch_a236:;
    m.ax() = uint16_t(0x94);
    trigger_sound_effect_core(m);
    goto branch_a278;
branch_a23e:;
    m.bx() = uint16_t(
        (m.bx() & 0x00FF) |
        (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx() >> 8) ^ uint8_t(m.bx() >> 8)))))
         << 8));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.bx() = uint16_t(computed::shift_left<uint16_t>(m, m.bx(), 0x1));
    m.ax() = uint16_t(m.rd16(m.cs(), uint16_t(m.bx() + 0xA253)));
    {
        const uint16_t callback_cs = m.cs();
        dispatch_computed_handler_core(m, m.ax());
        if (m.cs() != callback_cs)
            return;
    }
    if (m.flags() & emu::CF)
        goto branch_a278;
    {
        const uint16_t callback_cs = m.cs();
        draw_shop_purchase_confirmation_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    goto branch_a21d;
branch_a257:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x1)));
    if (!(m.flags() & emu::CF))
        goto branch_a26d;
    m.set_al(uint8_t(uint8_t(m.dx())));
    goto branch_a26d;
branch_a262:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.dx()));
    if (m.flags() & (emu::CF | emu::ZF))
        goto branch_a26d;
    m.set_al(uint8_t(0x0));
branch_a26d:;
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    m.ax() = uint16_t(0x92);
    trigger_sound_effect_core(m);
    goto branch_a21d;
branch_a278:;
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x14));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x6)) << 8));
    m.cx() = uint16_t(0x405);
    m.si() = uint16_t(kMenuScreenBackup);
    m.di() = uint16_t(kScreenMap2);
    {
        const uint16_t callback_cs = m.cs();
        copy_screen_rectangle_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.flags() &= ~emu::CF;
    return;
}
void confirm_shop_purchase(Machine &m) {
    confirm_shop_purchase_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x34A28A (near)
// Buy into a compatible free equipment slot; reject monsters, restricted slots or duplicate armor categories.
void buy_and_equip_shop_item_core(Machine &m) {
    UiSceneScope ui_scope(m, hooks::Event::ServiceMenu, 6);
    m.ax() = uint16_t(0x93);
    trigger_sound_effect_core(m);
    m.set_al(uint8_t(0x2));
    {
        const uint16_t callback_cs = m.cs();
        show_shop_submenu_cursor_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        draw_shop_equipment_member_screen_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        draw_shop_equipment_member_cursor_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    dma_sprite_table_core(m);
    dma_palettes_core(m);
    m.set_al(uint8_t(0x2));
    {
        const uint16_t callback_cs = m.cs();
        blink_shop_submenu_cursors_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.call_far(0x4000, 0x0009);
branch_a2af:;
    m.call_far(0x4000, 0x005C);
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kKeysRepeat)));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x3));
    m.si() = uint16_t(kMenuThirdSelection);
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x8));
    if (m.flags() & emu::ZF)
        goto branch_a2c5;
    goto branch_a440;
branch_a2c5:;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x4));
    if (!(m.flags() & emu::ZF))
        goto branch_a2f3;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x10));
    if (m.flags() & emu::ZF)
        goto branch_a2d2;
    goto branch_a40e;
branch_a2d2:;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x40));
    if (m.flags() & emu::ZF)
        goto branch_a2da;
    goto branch_a426;
branch_a2da:;
    hooks::emit(hooks::Event::ServiceMenu, m, {6});
    {
        const uint16_t callback_cs = m.cs();
        draw_shop_equipment_member_cursor_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    dma_sprite_table_core(m);
    dma_palettes_core(m);
    m.set_al(uint8_t(0x2));
    {
        const uint16_t callback_cs = m.cs();
        blink_shop_submenu_cursors_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.call_far(0x4000, 0x0009);
    goto branch_a2af;
branch_a2f3:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.si() = uint16_t(kGameState);
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0x1F));
    {
        const uint16_t product = uint16_t(m.al()) * uint8_t(m.dx());
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.ax()));
    (void)computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x000A)) & 0x20));
    if (!(m.flags() & emu::ZF))
        goto branch_a319;
    (void)computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x000A)) & 0x80));
    if (!(m.flags() & emu::ZF))
        goto branch_a311;
    goto branch_a343;
branch_a311:;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xF));
    m.cx() = uint16_t(0x8);
    goto branch_a321;
branch_a319:;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x17));
    m.cx() = uint16_t(0x4);
    goto branch_a321;
branch_a321:;
    (void)computed::subtract<uint8_t>(m, m.rd8(m.ss(), uint16_t(m.si() + 0x0001)), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_a39d;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    if (--m.cx())
        goto branch_a321;
    {
        hooks::emit(hooks::Event::ServiceMenu, m, {13, 0, int32_t(TextKey::buy_and_equip_shop_item_1)});
        const TextPtr text_ptr = text(m, TextKey::buy_and_equip_shop_item_1);
        const uint16_t caller_ds = m.ds(); // the original draws through the caller's DS (= CS)
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t(0xE05);
        if (text_ptr.segment == kTextSegment)
            m.ds() = text_ptr.segment;
        m.si() = text_ptr.offset;
        draw_text_core(m);
        m.ds() = caller_ds;
    }
    m.ax() = uint16_t(0x2A);
    trigger_sound_effect_core(m);
    goto branch_a36f;
branch_a343:;
    {
        hooks::emit(hooks::Event::ServiceMenu, m, {13, 0, int32_t(TextKey::buy_and_equip_shop_item_2)});
        const TextPtr text_ptr = text(m, TextKey::buy_and_equip_shop_item_2);
        const uint16_t caller_ds = m.ds(); // the original draws through the caller's DS (= CS)
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t(0xE05);
        if (text_ptr.segment == kTextSegment)
            m.ds() = text_ptr.segment;
        m.si() = text_ptr.offset;
        draw_text_core(m);
        m.ds() = caller_ds;
    }
    m.ax() = uint16_t(0x2A);
    trigger_sound_effect_core(m);
    goto branch_a36f;
branch_a359:;
    {
        hooks::emit(hooks::Event::ServiceMenu, m, {13, 0, int32_t(TextKey::buy_and_equip_shop_item_3)});
        const TextPtr text_ptr = text(m, TextKey::buy_and_equip_shop_item_3);
        const uint16_t caller_ds = m.ds(); // the original draws through the caller's DS (= CS)
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t(0xE05);
        if (text_ptr.segment == kTextSegment)
            m.ds() = text_ptr.segment;
        m.si() = text_ptr.offset;
        draw_text_core(m);
        m.ds() = caller_ds;
    }
    m.ax() = uint16_t(0x2A);
    trigger_sound_effect_core(m);
    goto branch_a36f;
branch_a36f:;
    m.set_al(uint8_t(0x2));
    {
        const uint16_t callback_cs = m.cs();
        blink_shop_submenu_cursors_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    dma_sprite_table_core(m);
    m.call_far(0x4000, 0x0009);
    m.call_far(0x4000, 0x005C);
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kKeysPressed)));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | m.ax())));
    if (m.flags() & emu::ZF)
        goto branch_a36f;
    {
        const TextPtr text_ptr = text(m, TextKey::buy_and_equip_shop_item_4);
        const uint16_t caller_ds = m.ds(); // the original draws through the caller's DS (= CS)
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t(0xE05);
        if (text_ptr.segment == kTextSegment)
            m.ds() = text_ptr.segment;
        m.si() = text_ptr.offset;
        draw_text_core(m);
        m.ds() = caller_ds;
    }
    m.flags() &= ~emu::CF;
    goto branch_a2da;
branch_a39d:;
    m.di() = uint16_t(kNameInsertionIndex);
    replace_equipment_stats_core(m);
    if (m.flags() & emu::CF)
        goto branch_a359;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kNameInsertionIndex))));
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kNameText))));
    m.wr8(m.ss(), uint16_t(m.si() + 0x0001), uint8_t(m.al()));
    m.dx() = uint16_t(m.rd16(m.ss(), uint16_t(kGoldHigh)));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kGoldLow)));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kMenuPriceLow))));
    m.dx() = uint16_t(computed::subtract<uint16_t>(m, m.dx(), m.rd16(m.ss(), uint16_t(kMenuPriceHigh)),
                                                   bool(m.flags() & emu::CF)));
    m.wr16(m.ss(), uint16_t(kGoldHigh), uint16_t(m.dx()));
    m.wr16(m.ss(), uint16_t(kGoldLow), uint16_t(m.ax()));
    m.ax() = uint16_t(0x1);
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kNameInsertionIndex))));
    m.bx() = uint16_t(0xE05);
    // {item} (text_tokens.h): the name inside the string, which then starts where the name would
    {
        TextValues values;
        values.set(U"item", name_text(m, NameTable::Item, uint8_t(m.dx())));
        if (!has_value_tokens(m, TextKey::buy_and_equip_shop_item_5)) {
            const uint16_t callback_cs = m.cs();
            draw_shop_item_name_core(m);
            if (m.cs() != callback_cs)
                return;
        }
        {
            hooks::emit(hooks::Event::ServiceMenu, m, {13, 0, int32_t(TextKey::buy_and_equip_shop_item_5)});
            const TextPtr text_ptr = text(m, TextKey::buy_and_equip_shop_item_5);
            const uint16_t caller_ds = m.ds(); // the original draws through the caller's DS (= CS)
            m.ax() = uint16_t(0x1);
            if (text_ptr.segment == kTextSegment)
                m.ds() = text_ptr.segment;
            m.si() = text_ptr.offset;
            draw_text_core(m);
            m.ds() = caller_ds;
        }
    }
    m.ax() = uint16_t(0xA1);
    trigger_sound_effect_core(m);
branch_a3ef:;
    m.set_al(uint8_t(0x2));
    {
        const uint16_t callback_cs = m.cs();
        blink_shop_submenu_cursors_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    dma_sprite_table_core(m);
    m.call_far(0x4000, 0x0009);
    m.call_far(0x4000, 0x005C);
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kKeysPressed)));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | m.ax())));
    if (m.flags() & emu::ZF)
        goto branch_a3ef;
    m.flags() |= emu::CF;
    goto branch_a449;
branch_a40e:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x1)));
    if (!(m.flags() & emu::CF))
        goto branch_a417;
    m.set_al(uint8_t(uint8_t(m.dx())));
branch_a417:;
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    select_previous_party_member_core(m);
    m.ax() = uint16_t(0x92);
    trigger_sound_effect_core(m);
    goto branch_a2da;
branch_a426:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.dx()));
    if (m.flags() & (emu::CF | emu::ZF))
        goto branch_a431;
    m.set_al(uint8_t(0x0));
branch_a431:;
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    select_next_party_member_core(m);
    m.ax() = uint16_t(0x92);
    trigger_sound_effect_core(m);
    goto branch_a2da;
branch_a440:;
    m.ax() = uint16_t(0x94);
    trigger_sound_effect_core(m);
    m.flags() &= ~emu::CF;
    goto branch_a449;
branch_a449:;
    computed::push(m, uint16_t(m.flags() | 0xF002));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x2));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x3)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xB));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    m.si() = uint16_t(kOverlayScreen);
    m.di() = uint16_t(kScreenMap);
    {
        const uint16_t callback_cs = m.cs();
        copy_screen_rectangle_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x2));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x3)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xB));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    m.si() = uint16_t(kMenuScreenBackup);
    m.di() = uint16_t(kScreenMap2);
    {
        const uint16_t callback_cs = m.cs();
        copy_screen_rectangle_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(0x2));
    {
        const uint16_t callback_cs = m.cs();
        hide_shop_submenu_cursor_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.flags() = uint16_t(computed::pop(m) & 0x0FD5);
    return;
}
void buy_and_equip_shop_item(Machine &m) {
    buy_and_equip_shop_item_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x34A473 (near)
// Buy into the first empty inventory slot and commit the previously computed Gil balance.
void buy_shop_item_into_inventory_core(Machine &m) {
    UiSceneScope ui_scope(m, hooks::Event::ServiceMenu, 4);
    m.si() = uint16_t(kInventory);
    m.cx() = uint16_t(0x8);
branch_a479:;
    (void)computed::subtract<uint8_t>(m, m.rd8(m.ss(), uint16_t(m.si() + 0x0001)), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_a4b8;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    if (--m.cx())
        goto branch_a479;
    {
        hooks::emit(hooks::Event::ServiceMenu, m, {13, 0, int32_t(TextKey::buy_shop_item_into_inventory_1)});
        const TextPtr text_ptr = text(m, TextKey::buy_shop_item_into_inventory_1);
        const uint16_t caller_ds = m.ds(); // the original draws through the caller's DS (= CS)
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t(0xE05);
        if (text_ptr.segment == kTextSegment)
            m.ds() = text_ptr.segment;
        m.si() = text_ptr.offset;
        draw_text_core(m);
        m.ds() = caller_ds;
    }
    m.ax() = uint16_t(0x2A);
    trigger_sound_effect_core(m);
branch_a499:;
    m.set_al(uint8_t(0x1));
    {
        const uint16_t callback_cs = m.cs();
        blink_shop_submenu_cursors_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    dma_sprite_table_core(m);
    m.call_far(0x4000, 0x0009);
    m.call_far(0x4000, 0x005C);
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kKeysPressed)));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | m.ax())));
    if (m.flags() & emu::ZF)
        goto branch_a499;
    m.flags() &= ~emu::CF;
    goto branch_a51f;
branch_a4b8:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kNameInsertionIndex))));
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kNameText))));
    m.wr8(m.ss(), uint16_t(m.si() + 0x0001), uint8_t(m.al()));
    m.dx() = uint16_t(m.rd16(m.ss(), uint16_t(kGoldHigh)));
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kGoldLow)));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kMenuPriceLow))));
    m.dx() = uint16_t(computed::subtract<uint16_t>(m, m.dx(), m.rd16(m.ss(), uint16_t(kMenuPriceHigh)),
                                                   bool(m.flags() & emu::CF)));
    m.wr16(m.ss(), uint16_t(kGoldHigh), uint16_t(m.dx()));
    m.wr16(m.ss(), uint16_t(kGoldLow), uint16_t(m.ax()));
    m.ax() = uint16_t(0x1);
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kNameInsertionIndex))));
    m.bx() = uint16_t(0xE05);
    // {item} (text_tokens.h): the name inside the string, which then starts where the name would
    {
        TextValues values;
        values.set(U"item", name_text(m, NameTable::Item, uint8_t(m.dx())));
        if (!has_value_tokens(m, TextKey::buy_shop_item_into_inventory_2)) {
            const uint16_t callback_cs = m.cs();
            draw_shop_item_name_core(m);
            if (m.cs() != callback_cs)
                return;
        }
        {
            hooks::emit(hooks::Event::ServiceMenu, m,
                        {13, 0, int32_t(TextKey::buy_shop_item_into_inventory_2)});
            const TextPtr text_ptr = text(m, TextKey::buy_shop_item_into_inventory_2);
            const uint16_t caller_ds = m.ds(); // the original draws through the caller's DS (= CS)
            m.ax() = uint16_t(0x1);
            if (text_ptr.segment == kTextSegment)
                m.ds() = text_ptr.segment;
            m.si() = text_ptr.offset;
            draw_text_core(m);
            m.ds() = caller_ds;
        }
    }
    m.ax() = uint16_t(0xA1);
    trigger_sound_effect_core(m);
branch_a502:;
    m.set_al(uint8_t(0x1));
    {
        const uint16_t callback_cs = m.cs();
        blink_shop_submenu_cursors_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    dma_sprite_table_core(m);
    m.call_far(0x4000, 0x0009);
    m.call_far(0x4000, 0x005C);
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kKeysPressed)));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | m.ax())));
    if (m.flags() & emu::ZF)
        goto branch_a502;
    m.flags() |= emu::CF;
branch_a51f:;
    return;
}
void buy_shop_item_into_inventory(Machine &m) {
    buy_shop_item_into_inventory_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x34A520 (near)
// Draw the purchase prompt and its two destination choices.
void draw_shop_purchase_confirmation_core(Machine &m) {
    computed::push(m, m.cs());
    m.ds() = uint16_t(computed::pop(m));
    m.ax() = uint16_t(0x0);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x1));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xD)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1C));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x5)) << 8));
    draw_window_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x1));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xD)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1C));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x5)) << 8));
    draw_window_core(m);
    {
        const TextPtr text_ptr = text(m, TextKey::draw_shop_purchase_confirmation_1);
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t(0xE05);
        m.ds() = text_ptr.segment;
        m.si() = text_ptr.offset;
        draw_text_core(m);
        text_done(m, TextKey::draw_shop_purchase_confirmation_1);
    }
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x14));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x6)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x5));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x4)) << 8));
    draw_window_core(m);
    {
        const TextPtr text_ptr = text(m, TextKey::draw_shop_purchase_confirmation_2);
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x15));
        m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x7)) << 8));
        m.ds() = text_ptr.segment;
        m.si() = text_ptr.offset;
        draw_text_core(m);
        text_done(m, TextKey::draw_shop_purchase_confirmation_2);
    }
    {
        const TextPtr text_ptr = text(m, TextKey::draw_shop_purchase_confirmation_3);
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x15));
        m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x8)) << 8));
        m.ds() = text_ptr.segment;
        m.si() = text_ptr.offset;
        draw_text_core(m);
        text_done(m, TextKey::draw_shop_purchase_confirmation_3);
    }
    return;
}
void draw_shop_purchase_confirmation(Machine &m) {
    draw_shop_purchase_confirmation_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x34A57F (near)
// Price infinite-use items at half base; otherwise scale base by remaining uses / (2*maximum uses).
void draw_shop_sale_confirmation_core(Machine &m) {
    computed::push(m, m.cs());
    m.ds() = uint16_t(computed::pop(m));
    m.ax() = uint16_t(0x0);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x1));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xD)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1C));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x5)) << 8));
    draw_window_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x1));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xD)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1C));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x5)) << 8));
    draw_window_core(m);
    {
        const TextPtr text_ptr = text(m, TextKey::draw_shop_sale_confirmation_1);
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t(0xE05);
        m.ds() = text_ptr.segment;
        m.si() = text_ptr.offset;
        draw_text_core(m);
        text_done(m, TextKey::draw_shop_sale_confirmation_1);
    }
    m.ax() = uint16_t(0x9000);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kInventory);
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kMenuFourthSelection))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x1));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.ax()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.bx() = uint16_t(
        (m.bx() & 0x00FF) |
        (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx() >> 8) ^ uint8_t(m.bx() >> 8)))))
         << 8));
    computed::push(m, m.bx());
    m.bx() = uint16_t(computed::shift_left<uint16_t>(m, m.bx(), 0x1));
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), 0x41E0));
    m.bx() = uint16_t(m.rd16(m.es(), uint16_t(m.bx())));
    m.set_al(uint8_t(table_at(m, Table::item_records, m.es(), uint16_t(m.bx() + 0x0003))));
    m.bx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(
        (m.cx() & 0x00FF) |
        (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.cx() >> 8) ^ uint8_t(m.cx() >> 8)))))
         << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.bx() = uint16_t(computed::shift_left<uint16_t>(m, m.bx(), 0x2));
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), kItemPrices));
    m.ax() = uint16_t(table_at16(m, Table::item_prices, m.cs(), m.bx()));
    m.dx() = uint16_t(table_at16(m, Table::item_prices, m.cs(), uint16_t(m.bx() + 0x0002)));
    m.bx() = uint16_t(m.cx());
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFE);
    // Maximum uses FEh takes the half-price path; other items use the proportional-use formula.
    if (!(m.flags() & emu::ZF))
        goto branch_a5f6;
    m.dx() = uint16_t(computed::shift_right<uint16_t>(m, m.dx(), 0x1));
    m.ax() = uint16_t(computed::rotate_right<uint16_t>(m, m.ax(), 0x1));
    goto branch_a62a;
branch_a5f6:;
    computed::push(m, m.ax());
    computed::push(m, m.dx());
    m.dx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.dx() ^ m.dx())));
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0001))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x4));
    m.cx() = uint16_t(m.ax());
    m.dx() = uint16_t(computed::pop(m));
    m.ax() = uint16_t(computed::pop(m));
    // Start at one base price, then add it (16*remaining_uses - 1) times before dividing by 32 and maximum
    // uses.
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    m.si() = uint16_t(m.ax());
    m.di() = uint16_t(m.dx());
// Original-game bug kept: zero remaining uses wraps the countdown, producing 65,535 extra additions.
branch_a60c:;
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.si()));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.di(), bool(m.flags() & emu::CF)));
    if (--m.cx())
        goto branch_a60c;
    m.dx() = uint16_t(computed::shift_right<uint16_t>(m, m.dx(), 0x1));
    m.ax() = uint16_t(computed::rotate_right<uint16_t>(m, m.ax(), 0x1));
    m.dx() = uint16_t(computed::shift_right<uint16_t>(m, m.dx(), 0x1));
    m.ax() = uint16_t(computed::rotate_right<uint16_t>(m, m.ax(), 0x1));
    m.dx() = uint16_t(computed::shift_right<uint16_t>(m, m.dx(), 0x1));
    m.ax() = uint16_t(computed::rotate_right<uint16_t>(m, m.ax(), 0x1));
    m.dx() = uint16_t(computed::shift_right<uint16_t>(m, m.dx(), 0x1));
    m.ax() = uint16_t(computed::rotate_right<uint16_t>(m, m.ax(), 0x1));
    m.dx() = uint16_t(computed::shift_right<uint16_t>(m, m.dx(), 0x1));
    m.ax() = uint16_t(computed::rotate_right<uint16_t>(m, m.ax(), 0x1));
    {
        // After the five SHR/RCR pairs, divide floor(base_price*remaining_uses/2) by maximum uses.
        const uint32_t numerator = (uint32_t(m.dx()) << 16) | m.ax();
        const uint16_t divisor = m.bx();
        if (!divisor || numerator / divisor > 65535)
            return;
        m.ax() = uint16_t(numerator / divisor);
        m.dx() = uint16_t(numerator % divisor);
    }
    m.dx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.dx() ^ m.dx())));
branch_a62a:;
    m.wr16(m.ss(), uint16_t(kMenuPriceLow), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(kMenuPriceHigh), uint16_t(m.dx()));
    format_inn_cost_call_core(m);
    if (has_value_tokens(m, TextKey::draw_shop_sale_confirmation_1)) { // {gold}: the label again, with it
        TextValues values;
        values.set(U"gold", ram_text(m, 0, kGoldText));
        const TextPtr text_ptr = text(m, TextKey::draw_shop_sale_confirmation_1);
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t(0xE05);
        m.ds() = text_ptr.segment;
        m.si() = text_ptr.offset;
        draw_text_core(m);
        text_done(m, TextKey::draw_shop_sale_confirmation_1);
    } else {
        m.ax() = uint16_t(0x0);
        m.ds() = uint16_t(m.ax());
        m.bx() = uint16_t(0xE09);
        m.ax() = uint16_t(0x1);
        m.si() = uint16_t(kGoldText);
        draw_text_core(m);
    }
    computed::push(m, m.cs());
    m.ds() = uint16_t(computed::pop(m));
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x14));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x6)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x5));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x4)) << 8));
    draw_window_core(m);
    {
        const TextPtr text_ptr = text(m, TextKey::draw_shop_sale_confirmation_2);
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x15));
        m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x7)) << 8));
        m.ds() = text_ptr.segment;
        m.si() = text_ptr.offset;
        draw_text_core(m);
        text_done(m, TextKey::draw_shop_sale_confirmation_2);
    }
    {
        const TextPtr text_ptr = text(m, TextKey::draw_shop_sale_confirmation_3);
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x15));
        m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x8)) << 8));
        m.ds() = text_ptr.segment;
        m.si() = text_ptr.offset;
        draw_text_core(m);
        text_done(m, TextKey::draw_shop_sale_confirmation_3);
    }
    return;
}
void draw_shop_sale_confirmation(Machine &m) {
    draw_shop_sale_confirmation_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x34A67A (near)
// Draw three stock items, scroll arrows and gold, and cache the selected item's price.
void draw_shop_stock_page_core(Machine &m) {
    computed::push(m, m.ds());
    computed::push(m, m.es());
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x13));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x3)) << 8));
    draw_window_core(m);
    format_gold_core(m);
    {
        const TextPtr text_ptr = text(m, TextKey::draw_shop_stock_page_1);
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x14));
        m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xB)) << 8));
        m.ds() = text_ptr.segment;
        m.si() = text_ptr.offset;
        TextValues values; // {gold} (text_tokens.h): the amount in the string, not drawn over its gap
        values.set(U"gold", ram_text(m, 0, kGoldText));
        draw_text_core(m);
        text_done(m, TextKey::draw_shop_stock_page_1);
    }
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x14));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xB)) << 8));
    m.ax() = uint16_t(0x1);
    m.si() = uint16_t(kGoldText);
    if (!has_value_tokens(m, TextKey::draw_shop_stock_page_1))
        draw_text_core(m);
    computed::push(m, m.cs());
    m.ds() = uint16_t(computed::pop(m));
    m.ax() = uint16_t(0x9000);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kShopStock);
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kCreationLeader))));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0xA));
    {
        const uint16_t product = uint16_t(m.al()) * uint8_t(m.dx());
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.ax()));
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kCreationRace))));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.ax()));
    m.cx() = uint16_t(0x3);
    m.bx() = uint16_t(0xE05);
branch_a6d8:;
    computed::push(m, m.cx());
    computed::push(m, m.si());
    m.set_al(uint8_t(table_at(m, Table::shops, m.cs(), m.si())));
    {
        const uint16_t callback_cs = m.cs();
        draw_shop_stock_item_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.si() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::increment<uint16_t>(m, m.si()));
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), 0x100));
    if (--m.cx())
        goto branch_a6d8;
    (void)computed::subtract<uint8_t>(m, m.rd8(m.ss(), uint16_t(kCreationRace)), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_a704;
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0xD09);
    m.cx() = uint16_t(0x101);
    m.dx() = uint16_t(0x1FF5);
    fill_screen_rect_core(m);
    goto branch_a715;
branch_a704:;
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0xD09);
    m.cx() = uint16_t(0x101);
    m.dx() = uint16_t(0x1FD6);
    fill_screen_rect_core(m);
branch_a715:;
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kCreationRace))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::add<uint8_t>(m, uint8_t(m.bx()), 0x3)));
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), m.rd8(m.ss(), uint16_t(kNamePage)));
    if (m.flags() & emu::ZF)
        goto branch_a737;
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0x1109);
    m.cx() = uint16_t(0x101);
    m.dx() = uint16_t(0x1FF6);
    fill_screen_rect_core(m);
    goto branch_a748;
branch_a737:;
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0x1109);
    m.cx() = uint16_t(0x101);
    m.dx() = uint16_t(0x1FDC);
    fill_screen_rect_core(m);
branch_a748:;
    m.ax() = uint16_t(0x4000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x9000);
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t(kShopStock);
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kCreationLeader))));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0xA));
    {
        const uint16_t product = uint16_t(m.al()) * uint8_t(m.dx());
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.ax()));
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kCreationRace))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), m.rd8(m.ss(), uint16_t(kSaveSlotSelection)))));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.ax()));
    m.set_al(uint8_t(table_at(m, Table::shops, m.ds(), m.si())));
    m.wr8(m.ss(), uint16_t(kNameInsertionIndex), uint8_t(m.al()));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.bx() = uint16_t(
        (m.bx() & 0x00FF) |
        (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx() >> 8) ^ uint8_t(m.bx() >> 8)))))
         << 8));
    computed::push(m, m.bx());
    m.bx() = uint16_t(computed::shift_left<uint16_t>(m, m.bx(), 0x1));
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), 0x41E0));
    m.bx() = uint16_t(m.rd16(m.es(), uint16_t(m.bx())));
    m.set_al(uint8_t(table_at(m, Table::item_records, m.es(), uint16_t(m.bx() + 0x0003))));
    m.bx() = uint16_t(computed::pop(m));
    m.wr8(m.ss(), uint16_t(kNameText), uint8_t(m.al()));
    m.bx() = uint16_t(computed::shift_left<uint16_t>(m, m.bx(), 0x2));
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), kItemPrices));
    m.ax() = uint16_t(table_at16(m, Table::item_prices, m.ds(), m.bx()));
    m.wr16(m.ss(), uint16_t(kMenuPriceLow), uint16_t(m.ax()));
    m.dx() = uint16_t(table_at16(m, Table::item_prices, m.ds(), uint16_t(m.bx() + 0x0002)));
    m.wr16(m.ss(), uint16_t(kMenuPriceHigh), uint16_t(m.dx()));
    m.ds() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    return;
}
void draw_shop_stock_page(Machine &m) {
    draw_shop_stock_page_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x34A7A3 (near)
// Load the item's full 32-bit purchase price and maximum uses, then draw its stock row.
void draw_shop_stock_item_core(Machine &m) {
    computed::push(m, m.ds());
    computed::push(m, m.ax());
    m.ax() = uint16_t(0x9000);
    m.es() = uint16_t(m.ax());
    computed::push(m, m.cs());
    m.ds() = uint16_t(computed::pop(m));
    m.ax() = uint16_t(computed::pop(m));
    computed::push(m, m.bx());
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(uint8_t(m.bx())));
    m.bx() = uint16_t(
        (m.bx() & 0x00FF) |
        (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx() >> 8) ^ uint8_t(m.bx() >> 8)))))
         << 8));
    computed::push(m, m.bx());
    m.bx() = uint16_t(computed::shift_left<uint16_t>(m, m.bx(), 0x1));
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), 0x41E0));
    m.bx() = uint16_t(m.rd16(m.es(), uint16_t(m.bx())));
    m.dx() = uint16_t(
        (m.dx() & 0x00FF) |
        (uint16_t(uint8_t(table_at(m, Table::item_records, m.es(), uint16_t(m.bx() + 0x0003)))) << 8));
    m.bx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::shift_left<uint16_t>(m, m.bx(), 0x2));
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), kItemPrices));
    m.ax() = uint16_t(table_at16(m, Table::item_prices, m.cs(), m.bx()));
    m.wr16(m.ss(), uint16_t(kMenuPriceLow), uint16_t(m.ax()));
    m.ax() = uint16_t(table_at16(m, Table::item_prices, m.cs(), uint16_t(m.bx() + 0x0002)));
    m.wr16(m.ss(), uint16_t(kMenuPriceHigh), uint16_t(m.ax()));
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(computed::pop(m));
    computed::push(m, m.bx());
    draw_item_entry_call_core(m);
    m.bx() = uint16_t(computed::pop(m));
    computed::push(m, m.bx());
    format_inn_cost_call_core(m);
    m.bx() = uint16_t(computed::pop(m));
    computed::push(m, m.bx());
    {
        computed::push(m, m.cs());
        m.ds() = uint16_t(computed::pop(m));
        const TextPtr text_ptr = text(m, TextKey::draw_shop_stock_item_1);
        m.ds() = text_ptr.segment;
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x14));
        m.si() = text_ptr.offset;
        TextValues values; // {gold} (text_tokens.h): the amount in the string, not drawn over its gap
        values.set(U"gold", ram_text(m, 0, kGoldText));
        draw_text_core(m);
        text_done(m, TextKey::draw_shop_stock_item_1);
    }
    m.bx() = uint16_t(computed::pop(m));
    computed::push(m, m.bx());
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x14));
    m.ax() = uint16_t(0x1);
    m.si() = uint16_t(kGoldText);
    if (!has_value_tokens(m, TextKey::draw_shop_stock_item_1))
        draw_text_core(m);
    m.bx() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    return;
}
void draw_shop_stock_item(Machine &m) {
    draw_shop_stock_item_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x34A80E (near)
// Draw the item name without trailing blanks at AX and BX.
void draw_shop_item_name_core(Machine &m) {
    computed::push(m, m.ds());
    computed::push(m, m.ax());
    m.ax() = uint16_t(0x9000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::pop(m));
    computed::push(m, m.bx());
    computed::push(m, m.dx());
    m.si() = uint16_t(0x4BE0);
    m.bx() = uint16_t(
        (m.bx() & 0x00FF) |
        (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx() >> 8) ^ uint8_t(m.bx() >> 8)))))
         << 8));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(uint8_t(m.dx())));
    m.bx() = uint16_t(computed::shift_left<uint16_t>(m, m.bx(), 0x3));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.bx()));
    if (const TextPtr name = name_record(m, NameTable::Item, uint8_t(m.dx())); name.segment == kTextSegment) {
        m.ds() = name.segment; // a loaded language's name (text.h)
        m.si() = name.offset;
    }
    m.cx() = uint16_t(0x0);
    computed::push(m, m.si());
branch_a828:;
    (void)computed::subtract<uint8_t>(m, m.rd8(m.ds(), uint16_t(m.si())), 0xD9);
    if (m.flags() & emu::ZF)
        goto branch_a836;
    (void)computed::subtract<uint8_t>(m, m.rd8(m.ds(), uint16_t(m.si())), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_a836;
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx()));
    m.si() = uint16_t(computed::increment<uint16_t>(m, m.si()));
    goto branch_a828;
branch_a836:;
    m.si() = uint16_t(computed::pop(m));
    m.dx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    draw_text_n_white_core(m);
    m.ds() = uint16_t(computed::pop(m));
    return;
}
void draw_shop_item_name(Machine &m) {
    draw_shop_item_name_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x34A83E (near)
// Place a submenu cursor beside the saved main cursor position.
void show_shop_submenu_cursor_core(Machine &m) {
    m.bx() = uint16_t(m.rd16(m.ss(), uint16_t(kMenuCursorPosition)));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::add<uint8_t>(m, uint8_t(m.bx()), 0x2)));
    m.set_al(uint8_t(computed::shift_left<uint8_t>(m, m.al(), 0x2)));
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.dx() = uint16_t(0x3FFB);
    set_sprite_16x16_core(m);
    return;
}
void show_shop_submenu_cursor(Machine &m) {
    show_shop_submenu_cursor_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x34A854 (near)
// Move AL's submenu cursor offscreen.
void hide_shop_submenu_cursor_core(Machine &m) {
    m.bx() = uint16_t(0xE0);
    m.set_al(uint8_t(computed::shift_left<uint8_t>(m, m.al(), 0x2)));
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.dx() = uint16_t(0x3FFB);
    set_sprite_16x16_core(m);
    return;
}
void hide_shop_submenu_cursor(Machine &m) {
    hide_shop_submenu_cursor_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x34A865 (near)
// Alternate the submenu cursor tiles each frame.
void blink_shop_submenu_cursors_core(Machine &m) {
    m.wr8(m.ss(), uint16_t(kShopCursorBlink),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kShopCursorBlink)))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_a8c4;
    m.cx() = uint16_t(
        (m.cx() & 0x00FF) |
        (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.cx() >> 8) ^ uint8_t(m.cx() >> 8)))))
         << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0x1);
branch_a875:;
    computed::push(m, m.ax());
    m.set_al(uint8_t(computed::shift_left<uint8_t>(m, m.al(), 0x2)));
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    (void)computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ss(), uint16_t(kShopCursorBlink)) & 0x1));
    if (m.flags() & emu::ZF)
        goto branch_a8a6;
    m.dx() = uint16_t(0x3FFB);
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x2));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), kSpriteBuffer));
    m.di() = uint16_t(m.ax());
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.dx()));
    m.dx() = uint16_t(computed::increment<uint16_t>(m, m.dx()));
    m.wr16(m.ss(), uint16_t(m.di() + 0x0004), uint16_t(m.dx()));
    m.dx() = uint16_t(computed::increment<uint16_t>(m, m.dx()));
    m.wr16(m.ss(), uint16_t(m.di() + 0x0008), uint16_t(m.dx()));
    m.dx() = uint16_t(computed::increment<uint16_t>(m, m.dx()));
    m.wr16(m.ss(), uint16_t(m.di() + 0x000C), uint16_t(m.dx()));
    m.ax() = uint16_t(computed::pop(m));
    m.ax() = uint16_t(computed::increment<uint16_t>(m, m.ax()));
    if (--m.cx())
        goto branch_a875;
    goto branch_a8c4;
branch_a8a6:;
    m.dx() = uint16_t(0x21FF);
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x2));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), kSpriteBuffer));
    m.di() = uint16_t(m.ax());
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.dx()));
    m.wr16(m.ss(), uint16_t(m.di() + 0x0004), uint16_t(m.dx()));
    m.wr16(m.ss(), uint16_t(m.di() + 0x0008), uint16_t(m.dx()));
    m.wr16(m.ss(), uint16_t(m.di() + 0x000C), uint16_t(m.dx()));
    m.ax() = uint16_t(computed::pop(m));
    m.ax() = uint16_t(computed::increment<uint16_t>(m, m.ax()));
    if (--m.cx())
        goto branch_a875;
branch_a8c4:;
    return;
}
void blink_shop_submenu_cursors(Machine &m) {
    blink_shop_submenu_cursors_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x34A8C5 (near)
// Draw and save the buy/sell/exit command cursor position.
void draw_shop_command_cursor_core(Machine &m) {
    m.bx() = uint16_t(
        (m.bx() & 0x00FF) |
        (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx() >> 8) ^ uint8_t(m.bx() >> 8)))))
         << 8));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kMenuSelection))));
    m.bx() = uint16_t(computed::shift_left<uint16_t>(m, m.bx(), 0x1));
    m.ax() = uint16_t(m.rd16(m.cs(), uint16_t(m.bx() + 0xA8E6)));
    m.bx() = uint16_t(m.ax());
    m.wr16(m.ss(), uint16_t(kMenuCursorPosition), uint16_t(m.bx()));
    m.ax() = uint16_t(0x0);
    m.dx() = uint16_t(0x3FFB);
    set_sprite_16x16_core(m);
    return;
}
void draw_shop_command_cursor(Machine &m) {
    draw_shop_command_cursor_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x34A8EC (near)
// Draw and save the cursor for the selected stock row.
void draw_shop_stock_cursor_core(Machine &m) {
    m.bx() = uint16_t(
        (m.bx() & 0x00FF) |
        (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx() >> 8) ^ uint8_t(m.bx() >> 8)))))
         << 8));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kSaveSlotSelection))));
    m.bx() = uint16_t(computed::shift_left<uint16_t>(m, m.bx(), 0x1));
    m.ax() = uint16_t(m.rd16(m.cs(), uint16_t(m.bx() + 0xA90D)));
    m.bx() = uint16_t(m.ax());
    m.wr16(m.ss(), uint16_t(kMenuCursorPosition), uint16_t(m.bx()));
    m.ax() = uint16_t(0x0);
    m.dx() = uint16_t(0x3FFB);
    set_sprite_16x16_core(m);
    return;
}
void draw_shop_stock_cursor(Machine &m) {
    draw_shop_stock_cursor_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x34A913 (near)
// Draw and save the purchase-destination cursor position.
void draw_shop_purchase_cursor_core(Machine &m) {
    m.bx() = uint16_t(
        (m.bx() & 0x00FF) |
        (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx() >> 8) ^ uint8_t(m.bx() >> 8)))))
         << 8));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kSaveChoice))));
    m.bx() = uint16_t(computed::shift_left<uint16_t>(m, m.bx(), 0x1));
    m.ax() = uint16_t(m.rd16(m.cs(), uint16_t(m.bx() + 0xA934)));
    m.bx() = uint16_t(m.ax());
    m.wr16(m.ss(), uint16_t(kMenuCursorPosition), uint16_t(m.bx()));
    m.ax() = uint16_t(0x0);
    m.dx() = uint16_t(0x3FFB);
    set_sprite_16x16_core(m);
    return;
}
void draw_shop_purchase_cursor(Machine &m) {
    draw_shop_purchase_cursor_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x34A938 (near)
// Draw and save the cursor for the member receiving purchased equipment.
void draw_shop_equipment_member_cursor_core(Machine &m) {
    m.bx() = uint16_t(
        (m.bx() & 0x00FF) |
        (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx() >> 8) ^ uint8_t(m.bx() >> 8)))))
         << 8));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kMenuThirdSelection))));
    m.bx() = uint16_t(computed::shift_left<uint16_t>(m, m.bx(), 0x1));
    m.ax() = uint16_t(m.rd16(m.cs(), uint16_t(m.bx() + 0xA959)));
    m.bx() = uint16_t(m.ax());
    m.wr16(m.ss(), uint16_t(kMenuCursorPosition), uint16_t(m.bx()));
    m.ax() = uint16_t(0x0);
    m.dx() = uint16_t(0x3FFB);
    set_sprite_16x16_core(m);
    return;
}
void draw_shop_equipment_member_cursor(Machine &m) {
    draw_shop_equipment_member_cursor_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x34A961 (near)
// Draw and save the selected inventory row's sale cursor.
void draw_shop_sell_item_cursor_core(Machine &m) {
    m.bx() = uint16_t(
        (m.bx() & 0x00FF) |
        (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx() >> 8) ^ uint8_t(m.bx() >> 8)))))
         << 8));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kMenuFourthSelection))));
    m.bx() = uint16_t(computed::shift_left<uint16_t>(m, m.bx(), 0x1));
    m.ax() = uint16_t(m.rd16(m.cs(), uint16_t(m.bx() + 0xA982)));
    m.bx() = uint16_t(m.ax());
    m.wr16(m.ss(), uint16_t(kMenuCursorPosition), uint16_t(m.bx()));
    m.ax() = uint16_t(0x0);
    m.dx() = uint16_t(0x3FFB);
    set_sprite_16x16_core(m);
    return;
}
void draw_shop_sell_item_cursor(Machine &m) {
    draw_shop_sell_item_cursor_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x34A992 (near)
// Draw and save the cursor selected by the secondary shop choice index.
void draw_shop_secondary_choice_cursor_core(Machine &m) {
    m.bx() = uint16_t(
        (m.bx() & 0x00FF) |
        (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx() >> 8) ^ uint8_t(m.bx() >> 8)))))
         << 8));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kMenuFifthSelection))));
    m.bx() = uint16_t(computed::shift_left<uint16_t>(m, m.bx(), 0x1));
    m.ax() = uint16_t(m.rd16(m.cs(), uint16_t(m.bx() + 0xA9B3)));
    m.bx() = uint16_t(m.ax());
    m.wr16(m.ss(), uint16_t(kMenuCursorPosition), uint16_t(m.bx()));
    m.ax() = uint16_t(0x0);
    m.dx() = uint16_t(0x3FFB);
    set_sprite_16x16_core(m);
    return;
}
void draw_shop_secondary_choice_cursor(Machine &m) {
    draw_shop_secondary_choice_cursor_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x34A9B7 (near)
// Draw shop title, command list, instructions and gold windows.
void draw_shop_main_screen_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    (void)computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ss(), uint16_t(kCreationLeader)) & 0x1));
    if (!(m.flags() & emu::ZF))
        goto branch_a9e6;
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x1));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x0)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x8));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x3)) << 8));
    draw_window_core(m);
    {
        const TextPtr text_ptr = text(m, TextKey::draw_shop_main_screen_1);
        m.ds() = text_ptr.segment;
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x3));
        m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x1)) << 8));
        m.si() = text_ptr.offset;
        draw_text_core(m);
        text_done(m, TextKey::draw_shop_main_screen_1);
    }
    goto branch_aa09;
branch_a9e6:;
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x1));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x0)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x7));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x3)) << 8));
    draw_window_core(m);
    {
        const TextPtr text_ptr = text(m, TextKey::draw_shop_main_screen_2);
        m.ds() = text_ptr.segment;
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x3));
        m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x1)) << 8));
        m.si() = text_ptr.offset;
        draw_text_core(m);
        text_done(m, TextKey::draw_shop_main_screen_2);
    }
    goto branch_aa09;
branch_aa09:;
    m.ax() = uint16_t(0x0);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x1));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xD)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1C));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x5)) << 8));
    draw_window_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x1));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xD)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1C));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x5)) << 8));
    draw_window_core(m);
    {
        const TextPtr text_ptr = text(m, TextKey::draw_shop_main_screen_3);
        m.ds() = text_ptr.segment;
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t(0xE05);
        m.si() = text_ptr.offset;
        draw_text_core(m);
        text_done(m, TextKey::draw_shop_main_screen_3);
    }
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x19));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x5)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x4));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x5)) << 8));
    draw_window_core(m);
    {
        const TextPtr text_ptr = text(m, TextKey::draw_shop_main_screen_4);
        m.ds() = text_ptr.segment;
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x1A));
        m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x6)) << 8));
        m.si() = text_ptr.offset;
        draw_text_core(m);
        text_done(m, TextKey::draw_shop_main_screen_4);
    }
    {
        const TextPtr text_ptr = text(m, TextKey::draw_shop_main_screen_5);
        m.ds() = text_ptr.segment;
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x1A));
        m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x7)) << 8));
        m.si() = text_ptr.offset;
        draw_text_core(m);
        text_done(m, TextKey::draw_shop_main_screen_5);
    }
    {
        const TextPtr text_ptr = text(m, TextKey::draw_shop_main_screen_6);
        m.ds() = text_ptr.segment;
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x1A));
        m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x8)) << 8));
        m.si() = text_ptr.offset;
        draw_text_core(m);
        text_done(m, TextKey::draw_shop_main_screen_6);
    }
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x13));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x3)) << 8));
    draw_window_core(m);
    format_gold_core(m);
    {
        const TextPtr text_ptr = text(m, TextKey::draw_shop_main_screen_7);
        m.ds() = text_ptr.segment;
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x14));
        m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xB)) << 8));
        m.si() = text_ptr.offset;
        TextValues values; // {gold} (text_tokens.h): the amount in the string, not drawn over its gap
        values.set(U"gold", ram_text(m, 0, kGoldText));
        draw_text_core(m);
        text_done(m, TextKey::draw_shop_main_screen_7);
    }
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x14));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xB)) << 8));
    m.ax() = uint16_t(0x1);
    m.si() = uint16_t(kGoldText);
    if (!has_value_tokens(m, TextKey::draw_shop_main_screen_7))
        draw_text_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x1));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x10)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1C));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x2)) << 8));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void draw_shop_main_screen(Machine &m) {
    draw_shop_main_screen_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x34AABA (near)
// Draw the member-selection window and compact party summaries for a purchase.
void draw_shop_equipment_member_screen_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x1));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xD)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1C));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x5)) << 8));
    draw_window_core(m);
    {
        const TextPtr text_ptr = text(m, TextKey::draw_shop_equipment_member_screen_1);
        const uint16_t caller_ds = m.ds(); // the original draws through the caller's DS (= CS)
        if (text_ptr.segment == kTextSegment)
            m.ds() = text_ptr.segment;
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t(0xE05);
        m.si() = text_ptr.offset;
        draw_text_core(m);
        m.ds() = caller_ds;
    }
    m.ax() = uint16_t(0x0);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x2));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x3)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xB));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    draw_window_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x2));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x3)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xB));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    draw_window_core(m);
    load_compact_party_graphics_call_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0x403);
    m.dx() = uint16_t(0x0);
    draw_compact_member_icon_call_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0x603);
    m.dx() = uint16_t(0x1);
    draw_compact_member_icon_call_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0x803);
    m.dx() = uint16_t(0x2);
    draw_compact_member_icon_call_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0xA03);
    m.dx() = uint16_t(0x3);
    draw_compact_member_icon_call_core(m);
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0x405);
    m.si() = uint16_t(kGameState);
    draw_inn_member_summary_call_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0x605);
    m.si() = uint16_t(kMember1Record);
    draw_inn_member_summary_call_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0x805);
    m.si() = uint16_t(kMember2Record);
    draw_inn_member_summary_call_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0xA05);
    m.si() = uint16_t(kMember3Record);
    draw_inn_member_summary_call_core(m);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void draw_shop_equipment_member_screen(Machine &m) {
    draw_shop_equipment_member_screen_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x34AB69 (near)
// Draw inventory entries and current gold for the sale menu.
void draw_shop_sale_inventory_core(Machine &m) {
    computed::push(m, m.ss());
    m.ds() = uint16_t(computed::pop(m));
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x13));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xA));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x3)) << 8));
    draw_window_core(m);
    format_gold_core(m);
    {
        const TextPtr text_ptr = text(m, TextKey::draw_shop_sale_inventory_1);
        m.ds() = text_ptr.segment;
        m.ax() = uint16_t(0x1);
        m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x14));
        m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xB)) << 8));
        m.si() = text_ptr.offset;
        TextValues values; // {gold} (text_tokens.h): the amount in the string, not drawn over its gap
        values.set(U"gold", ram_text(m, 0, kGoldText));
        draw_text_core(m);
        text_done(m, TextKey::draw_shop_sale_inventory_1);
    }
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x14));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xB)) << 8));
    m.ax() = uint16_t(0x1);
    m.si() = uint16_t(kGoldText);
    if (!has_value_tokens(m, TextKey::draw_shop_sale_inventory_1))
        draw_text_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x2));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x3)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xD));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    draw_window_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x3));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x4)) << 8));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.rd8(m.ds(), uint16_t(kInventory))));
    m.dx() =
        uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(m.rd8(m.ds(), uint16_t(kInventorySlot0Uses)))) << 8));
    draw_item_entry_call_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x3));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x5)) << 8));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.rd8(m.ds(), uint16_t(kInventorySlot1Id))));
    m.dx() =
        uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(m.rd8(m.ds(), uint16_t(kInventorySlot1Uses)))) << 8));
    draw_item_entry_call_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x3));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x6)) << 8));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.rd8(m.ds(), uint16_t(kInventorySlot2Id))));
    m.dx() =
        uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(m.rd8(m.ds(), uint16_t(kInventorySlot2Uses)))) << 8));
    draw_item_entry_call_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x3));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x7)) << 8));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.rd8(m.ds(), uint16_t(kInventorySlot3Id))));
    m.dx() =
        uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(m.rd8(m.ds(), uint16_t(kInventorySlot3Uses)))) << 8));
    draw_item_entry_call_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x3));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x8)) << 8));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.rd8(m.ds(), uint16_t(kInventorySlot4Id))));
    m.dx() =
        uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(m.rd8(m.ds(), uint16_t(kInventorySlot4Uses)))) << 8));
    draw_item_entry_call_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x3));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x9)) << 8));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.rd8(m.ds(), uint16_t(kInventorySlot5Id))));
    m.dx() =
        uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(m.rd8(m.ds(), uint16_t(kInventorySlot5Uses)))) << 8));
    draw_item_entry_call_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x3));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xA)) << 8));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.rd8(m.ds(), uint16_t(kInventorySlot6Id))));
    m.dx() =
        uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(m.rd8(m.ds(), uint16_t(kInventorySlot6Uses)))) << 8));
    draw_item_entry_call_core(m);
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x3));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xB)) << 8));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.rd8(m.ds(), uint16_t(kInventorySlot7Id))));
    m.dx() =
        uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(m.rd8(m.ds(), uint16_t(kInventorySlot7Uses)))) << 8));
    draw_item_entry_call_core(m);
    return;
}
void draw_shop_sale_inventory(Machine &m) {
    draw_shop_sale_inventory_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x34AC44 (near)
// Fill the shop description sprite row with the empty-item description.
void draw_empty_shop_description_sprites_core(Machine &m) {
    m.ax() = uint16_t(0x9000);
    m.es() = uint16_t(m.ax());
    m.set_al(uint8_t(0xFF));
    m.si() = uint16_t(0x53E0);
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.dx() = uint16_t(0x1B);
    {
        const uint32_t product = uint32_t(m.ax()) * m.dx();
        m.ax() = uint16_t(product);
        m.dx() = uint16_t(product >> 16);
        computed::flag(m, emu::CF | emu::OF, m.dx() != 0);
    }
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.ax()));
    m.di() = uint16_t(kSpriteBuffer);
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kMenuDescriptionSpriteStart))));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), 0xC));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x2));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), m.ax()));
    m.bx() = uint16_t(0x804);
    m.cx() = uint16_t(0x1A);
branch_ac6e:;
    m.dx() = uint16_t(
        (m.dx() & 0x00FF) |
        (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.dx() >> 8) ^ uint8_t(m.dx() >> 8)))))
         << 8));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.rd8(m.es(), uint16_t(m.si()))));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), 0x2F00));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.dx()));
    m.wr16(m.ss(), uint16_t(m.di() + 0x0002), uint16_t(m.bx()));
    m.si() = uint16_t(computed::increment<uint16_t>(m, m.si()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.bx() = uint16_t((m.bx() & 0x00FF) |
                      (uint16_t(uint8_t(computed::add<uint8_t>(m, uint8_t(m.bx() >> 8), 0x8))) << 8));
    if (--m.cx())
        goto branch_ac6e;
    return;
}
void draw_empty_shop_description_sprites(Machine &m) {
    draw_empty_shop_description_sprites_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x34AC88 (near)
// Draw the selected item's description as a row of sprite glyphs.
void draw_shop_description_sprites_core(Machine &m) {
    m.ax() = uint16_t(0x9000);
    m.es() = uint16_t(m.ax());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    (void)computed::subtract<uint8_t>(m, m.rd8(m.ss(), uint16_t(m.si() + 0x0001)), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_ac99;
    m.set_al(uint8_t(0xFF));
branch_ac99:;
    m.si() = uint16_t(0x53E0);
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.dx() = uint16_t(0x1B);
    {
        const uint32_t product = uint32_t(m.ax()) * m.dx();
        m.ax() = uint16_t(product);
        m.dx() = uint16_t(product >> 16);
        computed::flag(m, emu::CF | emu::OF, m.dx() != 0);
    }
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.ax()));
    m.di() = uint16_t(kSpriteBuffer);
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kMenuDescriptionSpriteStart))));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), 0xC));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x2));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), m.ax()));
    m.bx() = uint16_t(0x804);
    m.cx() = uint16_t(0x1A);
branch_acbc:;
    m.dx() = uint16_t(
        (m.dx() & 0x00FF) |
        (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.dx() >> 8) ^ uint8_t(m.dx() >> 8)))))
         << 8));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.rd8(m.es(), uint16_t(m.si()))));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), 0x2F00));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.dx()));
    m.wr16(m.ss(), uint16_t(m.di() + 0x0002), uint16_t(m.bx()));
    m.si() = uint16_t(computed::increment<uint16_t>(m, m.si()));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x4));
    m.bx() = uint16_t((m.bx() & 0x00FF) |
                      (uint16_t(uint8_t(computed::add<uint8_t>(m, uint8_t(m.bx() >> 8), 0x8))) << 8));
    if (--m.cx())
        goto branch_acbc;
    return;
}
void draw_shop_description_sprites(Machine &m) {
    draw_shop_description_sprites_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x34ACD6 (near)
// Draw the description window and extend the visible sprite range by 26.
void open_shop_description_row_core(Machine &m) {
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x1));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x0)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1C));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x2)) << 8));
    draw_window_core(m);
    m.set_al(uint8_t(m.in8(uint8_t(0x6))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x1A)));
    m.out8(uint8_t(0x6), m.al());
    return;
}
void open_shop_description_row(Machine &m) {
    open_shop_description_row_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
// rom: 0x34ACED (near)
// Restore the description screen rectangle and remove its 26 sprites.
void close_shop_description_row_core(Machine &m) {
    m.set_al(uint8_t(m.in8(uint8_t(0x6))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x1A)));
    m.out8(uint8_t(0x6), m.al());
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x1));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x0)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1C));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x2)) << 8));
    m.si() = uint16_t(kMenuScreenBackup);
    m.di() = uint16_t(kScreenMap2);
    {
        const uint16_t callback_cs = m.cs();
        copy_screen_rectangle_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    return;
}
void close_shop_description_row(Machine &m) {
    close_shop_description_row_core(m);
    if (m.cs() == 0x4000)
        m.ret();
}
} // namespace sawgod::game
