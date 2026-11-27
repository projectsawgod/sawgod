#include "game/bank34_misc.h"
#include "game/bank35_field.h"
#include "game/bank36_battle.h"
#include "game/bank36_misc.h"
#include "game/bank37.h"
#include "game/bank3c_title.h"
#include "game/bank3d_sound.h"
#include "game/computed_flags.h"
#include "game/computed_handlers.h"
#include "game/hooks.h"
#include "game/session.h"
#include "game/system.h"
#include "game/tables.h"
#include "game/text.h"
#include "game/text_render.h"
// Decompiled field-map drawing and state helpers (bank 0x35, segment 5000h).
#include "game/bank35_world.h"

#include "game/bank34.h"
#include <algorithm>
#include <bit>
#include <utility>

namespace sawgod::game {
namespace {
constexpr uint16_t kDisplayBackup = 0x3800;              // field_display_backup
constexpr uint16_t kMapMetatiles = 0x1900;               // metatile words within kMapSegment
constexpr uint16_t kNineStripTwoFrameTiles = 0xF580;     // banked-ROM animation source (DS=2000h)
constexpr uint16_t kFastTwoStripFourFrameTiles = 0xF180; // banked-ROM animation source (DS=2000h)
constexpr uint16_t kFastSingleFourFrameTiles = 0xEF80;   // banked-ROM animation source (DS=2000h)
constexpr uint16_t kMixedFourAndTwoFrameTiles = 0xCA00;  // banked-ROM animation source (DS=2000h)
constexpr uint16_t kAlternateFourStripTiles = 0xD000;    // banked-ROM animation source (DS=2000h)
constexpr uint16_t kScreenMap2 = 0x1800;
constexpr uint16_t kScreenMap = 0x1000;
constexpr uint16_t kLineCompareInterruptVector = 0x0090;
constexpr uint16_t kOverlayScreen = 0x2000; // overlay_screen_map
constexpr uint16_t kSpriteBuffer = 0xC000; // sprite staging; also the base of the masked C000-FFFF RAM window
[[maybe_unused]] constexpr uint16_t kSpriteSixTileAttribute = 0xC019;      // sprite_six_tile_attribute
constexpr uint16_t kPaletteBuffer = 0xC400;                                // palette_buffer
constexpr uint16_t kPaletteTarget = 0xC600;                                // palette_target_buffer
constexpr uint16_t kKeysHeld = 0xC800;                                     // keys_held
constexpr uint16_t kKeysPressed = 0xC804;                                  // keys_pressed
constexpr uint16_t kKeysRepeat = 0xC806;                                   // keys_repeat
[[maybe_unused]] constexpr uint16_t kKeyRepeatTimer = 0xC808;              // key_repeat_timer
constexpr uint16_t kRngCalls = 0xC80A;                                     // random_counter
constexpr uint16_t kFrames = 0xC80C;                                       // random_seed
constexpr uint16_t kOamCount = 0xC80F;                                     // sprite_unit_count
constexpr uint16_t kPlayerFacing = 0xC810;                                 // player_facing
constexpr uint16_t kBlinkEnabled = 0xC811;                                 // player_blink_enabled
constexpr uint16_t kFieldOverlayMode = 0xC812;                             // field_overlay_mode
constexpr uint16_t kFieldFrameWaiting = 0xC813;                            // field_frame_waiting
constexpr uint16_t kFieldWindowTopLeft = 0xC814;                           // field_window_top_left
constexpr uint16_t kFieldWindowBottomRight = 0xC816;                       // field_window_bottom_right
constexpr uint16_t kSavedSceneSp = 0xC818;                                 // intro_saved_sp
constexpr uint16_t kSceneIndex = 0xC81A;                                   // scene_index
constexpr uint16_t kIntroStrip = 0xC81C;                                   // intro_strip
constexpr uint16_t kBattleSceneScratch = 0xC81E;                           // battle_scene_scratch
constexpr uint16_t kMusicRequest = 0xC820;                                 // music_request
constexpr uint16_t kSfxRequest = 0xC821;                                   // sfx_request
constexpr uint16_t kMusicRequestPrev = 0xC822;                             // music_current
constexpr uint16_t kFieldEventPending = 0xC825;                            // field_event_pending
constexpr uint16_t kFieldEventKind = 0xC826;                               // field_event_kind
constexpr uint16_t kFieldEventArgument = 0xC827;                           // field_event_argument
constexpr uint16_t kInteractionState = 0xC82A;                             // field_interaction_state
constexpr uint16_t kDialogueSkipDelay = 0xC82B;                            // dialogue_skip_delay
constexpr uint16_t kDialogueFrames = 0xC82C;                               // dialogue_frames
constexpr uint16_t kFieldControlFlag = 0xC880;                             // field_control_flag
constexpr uint16_t kEncountersEnabled = 0xC881;                            // encounters_enabled
constexpr uint16_t kNpcBackup = 0xC885;                                    // npc_backup
constexpr uint16_t kGameState = 0xD000;                                    // game_state
constexpr uint16_t kMemberName = 0xD001;                                   // member_name
constexpr uint16_t kMemberType = 0xD005;                                   // member0_race
[[maybe_unused]] constexpr uint16_t kMemberEquipmentFlags = 0xD00A;        // member_equipment_flags
constexpr uint16_t kMember0Equipment = 0xD00F;                             // member0_equipment
constexpr uint16_t kMember1Record = 0xD01F;                                // member1_record
[[maybe_unused]] constexpr uint16_t kSecondMemberEquipmentFlags = 0xD029;  // second_member_equipment_flags
constexpr uint16_t kMember2Record = 0xD03E;                                // member2_record
[[maybe_unused]] constexpr uint16_t kThirdMemberEquipmentFlags = 0xD048;   // third_member_equipment_flags
constexpr uint16_t kMember3Record = 0xD05D;                                // member3_record
[[maybe_unused]] constexpr uint16_t kFourthMemberEquipmentFlags = 0xD067;  // fourth_member_equipment_flags
constexpr uint16_t kInventory = 0xD07C;                                    // inventory_slots
[[maybe_unused]] constexpr uint16_t kInventorySlot0Uses = 0xD07D;          // inventory_slot_0_uses
[[maybe_unused]] constexpr uint16_t kInventorySlot1Id = 0xD07E;            // inventory_slot_1_id
[[maybe_unused]] constexpr uint16_t kInventorySlot1Uses = 0xD07F;          // inventory_slot_1_uses
[[maybe_unused]] constexpr uint16_t kInventorySlot2Id = 0xD080;            // inventory_slot_2_id
[[maybe_unused]] constexpr uint16_t kInventorySlot2Uses = 0xD081;          // inventory_slot_2_uses
[[maybe_unused]] constexpr uint16_t kInventorySlot3Id = 0xD082;            // inventory_slot_3_id
[[maybe_unused]] constexpr uint16_t kInventorySlot3Uses = 0xD083;          // inventory_slot_3_uses
[[maybe_unused]] constexpr uint16_t kInventorySlot4Id = 0xD084;            // inventory_slot_4_id
[[maybe_unused]] constexpr uint16_t kInventorySlot4Uses = 0xD085;          // inventory_slot_4_uses
[[maybe_unused]] constexpr uint16_t kInventorySlot5Id = 0xD086;            // inventory_slot_5_id
[[maybe_unused]] constexpr uint16_t kInventorySlot5Uses = 0xD087;          // inventory_slot_5_uses
[[maybe_unused]] constexpr uint16_t kInventorySlot6Id = 0xD088;            // inventory_slot_6_id
[[maybe_unused]] constexpr uint16_t kInventorySlot6Uses = 0xD089;          // inventory_slot_6_uses
[[maybe_unused]] constexpr uint16_t kInventorySlot7Id = 0xD08A;            // inventory_slot_7_id
[[maybe_unused]] constexpr uint16_t kInventorySlot7Uses = 0xD08B;          // inventory_slot_7_uses
constexpr uint16_t kKeyItems = 0xD08C;                                     // key_items
[[maybe_unused]] constexpr uint16_t kGoldLow = 0xD0A2;                     // gold
[[maybe_unused]] constexpr uint16_t kGoldHigh = 0xD0A4;                    // gold_high
constexpr uint16_t kPackedProgress = 0xD0A6;                               // packed_progress
constexpr uint16_t kProgressionBits = 0xD0AE;                              // progression_bits
constexpr uint16_t kVehicleLocations = 0xD0AF;                             // vehicle_locations
constexpr uint16_t kSecondVehicleMap = 0xD0B3;                             // second_vehicle_map
constexpr uint16_t kSecondVehicleX = 0xD0B4;                               // second_vehicle_x
constexpr uint16_t kSecondVehicleY = 0xD0B5;                               // second_vehicle_y
constexpr uint16_t kSecondVehicleState = 0xD0B6;                           // second_vehicle_state
constexpr uint16_t kBestiaryFlags = 0xD0BB;                                // bestiary_flags
constexpr uint16_t kTravelMode = 0xD0D6;                                   // travel_mode
constexpr uint16_t kPreviousMap = 0xD0D7;                                  // previous_map
constexpr uint16_t kPreviousPlayerX = 0xD0D8;                              // previous_player_x
constexpr uint16_t kPreviousPlayerY = 0xD0D9;                              // previous_player_y
constexpr uint16_t kTowerCounter = 0xD0DA;                                 // tower_counter
constexpr uint16_t kCurrentFloor = 0xD0DB;                                 // current_floor
constexpr uint16_t kMapId = 0xD0DC;                                        // current_map
constexpr uint16_t kPlayerCell = 0xD0DD;                                   // player_x
constexpr uint16_t kPlayerY = 0xD0DE;                                      // player_y
constexpr uint16_t kDialogueLeader = 0xD0E3;                               // dialogue_leader
[[maybe_unused]] constexpr uint16_t kEndingSeen = 0xD0E5;                  // ending_seen
constexpr uint16_t kDialogueSpeed = 0xD0E6;                                // dialogue_speed
constexpr uint16_t kOptions = 0xD0E7;                                      // field_options
constexpr uint16_t kPlayerWalkToggle = 0xD500;                             // player_walk_toggle
constexpr uint16_t kFieldPreSpriteCallback = 0xD501;                       // field_pre_sprite_callback
[[maybe_unused]] constexpr uint16_t kFormationBackPaletteSource = 0xD502;  // formation_back_palette_source
constexpr uint16_t kFieldPostSpriteCallback = 0xD503;                      // field_post_sprite_callback
[[maybe_unused]] constexpr uint16_t kFormationBackMapSource = 0xD504;      // formation_back_map_source
constexpr uint16_t kFieldNpcCallback = 0xD505;                             // field_npc_callback
[[maybe_unused]] constexpr uint16_t kFormationBackTileCount = 0xD506;      // formation_back_tile_count
[[maybe_unused]] constexpr uint16_t kFormationBackPaletteVariant = 0xD508; // formation_back_palette_variant
constexpr uint16_t kBattleWipeScroll = 0xD509;                             // battle_wipe_scroll
constexpr uint16_t kBattleWipeFirstX = 0xD50A;                             // battle_wipe_first_x
constexpr uint16_t kBattleWipeSecondScroll = 0xD50B;                       // battle_wipe_second_scroll
constexpr uint16_t kBattleWipeSecondX = 0xD50C;                            // battle_wipe_second_x
constexpr uint16_t kBattleWipeThirdScroll = 0xD50D;                        // battle_wipe_third_scroll
constexpr uint16_t kBattleWipeThirdX = 0xD50E;                             // formation_middle_palette_source
constexpr uint16_t kBattleWipeFourthScroll = 0xD50F;                       // battle_wipe_fourth_scroll
constexpr uint16_t kBattleWipeFourthX = 0xD510;                            // battle_wipe_fourth_x
constexpr uint16_t kFieldScroll = 0xD511;                                  // field_scroll
[[maybe_unused]] constexpr uint16_t kFormationMiddleTileCount = 0xD512;    // formation_middle_tile_count
constexpr uint16_t kIntroOverlayProgress = 0xD513;                         // intro_overlay_progress
[[maybe_unused]] constexpr uint16_t kFormationMiddlePaletteVariant =
    0xD514;                                                                // formation_middle_palette_variant
constexpr uint16_t kMoveDelta = 0xD515;                                    // player_move_delta
[[maybe_unused]] constexpr uint16_t kFormationMiddleHeight = 0xD516;       // formation_middle_height
constexpr uint16_t kMoveShift = 0xD517;                                    // player_move_shift
[[maybe_unused]] constexpr uint16_t kFormationFrontTileSource = 0xD518;    // formation_front_tile_source
constexpr uint16_t kFieldFadeActive = 0xD519;                              // field_fade_active
constexpr uint16_t kFieldPaletteFirstFrame = 0xD51A;                       // field_palette_first_frame
constexpr uint16_t kFieldPaletteTimer = 0xD51B;                            // field_palette_timer
constexpr uint16_t kFieldPaletteSecondFrame = 0xD51C;                      // field_palette_second_frame
constexpr uint16_t kFieldPaletteSecondTimer = 0xD51D;                      // field_palette_second_timer
constexpr uint16_t kFieldTileAnimationFrame = 0xD51E;                      // field_tile_animation_frame
constexpr uint16_t kFieldTileAnimationTimer = 0xD51F;                      // field_tile_animation_timer
constexpr uint16_t kSpriteSetIndex = 0xD520;                               // player_sprite_set
constexpr uint16_t kPlayerSpriteFacing = 0xD521;                           // player_sprite_facing
constexpr uint16_t kOamBaseY = 0xD522;                                     // player_sprite_y
[[maybe_unused]] constexpr uint16_t kFormationFrontPaletteCount = 0xD523;  // formation_front_palette_count
constexpr uint16_t kOamBaseX = 0xD524;                                     // player_sprite_x
constexpr uint16_t kBlinkToggleA = 0xD526;                                 // player_blink_toggle_a
constexpr uint16_t kPlayerBlinkTimerA = 0xD527;                            // player_blink_timer_a
constexpr uint16_t kAnimationStep = 0xD528;                                // player_blink_period_a
constexpr uint16_t kFloorDamageLatched = 0xD529;                           // floor_damage_latched
constexpr uint16_t kPlayerStepped = 0xD52A;                                // player_stepped
constexpr uint16_t kOverlayEnabled = 0xD52B;                               // field_overlay_enabled
constexpr uint16_t kScriptActive = 0xD52C;                                 // field_script_active
constexpr uint16_t kFieldMapBank = 0xD52D;                                 // field_map_bank
constexpr uint16_t kFieldMapSource = 0xD52E;                               // formation_effect_palette_variant
[[maybe_unused]] constexpr uint16_t kFormationEffectWidth = 0xD52F;        // formation_effect_width
constexpr uint16_t kFieldMetatileSource = 0xD530;                          // formation_effect_height
[[maybe_unused]] constexpr uint16_t kFormationEffectPaletteCount = 0xD531; // formation_effect_palette_count
constexpr uint16_t kFieldMetatileMirrorSource = 0xD532;                    // field_metatile_mirror_source
constexpr uint16_t kFieldAttributeSource = 0xD534;                         // field_attribute_source
constexpr uint16_t kFieldAnimationTileSource = 0xD536;                     // field_animation_tile_source
constexpr uint16_t kFieldAnimationPaletteSource = 0xD538;                  // field_animation_palette_source
constexpr uint16_t kFieldTilesetIndex = 0xD53A;                            // field_tileset_index
constexpr uint16_t kTileWordFlags = 0xD53B;                                // map_tile_word_flags
constexpr uint16_t kTileByteFlags = 0xD57B;                                // map_tile_byte_flags
constexpr uint16_t kRunningAllowed = 0xD59C;                               // running_allowed
constexpr uint16_t kCamX = 0xD59D;                                         // camera_x
constexpr uint16_t kCamY = 0xD59F;                                         // camera_y
constexpr uint16_t kNpcLastMatch = 0xD5A1;                                 // npc_last_match
constexpr uint16_t kPlayerBlinkToggleB = 0xD5A3;                           // player_blink_toggle_b
constexpr uint16_t kPlayerBlinkTimerB = 0xD5A4;                            // player_blink_timer_b
constexpr uint16_t kNpcDirectionSpriteSets = 0xD5A6;                       // npc_direction_sprite_sets
constexpr uint16_t kVehicleSpriteSet = 0xD5AD;                             // vehicle_sprite_set
constexpr uint16_t kNpcTable = 0xD5AE;                                     // npc_records
[[maybe_unused]] constexpr uint16_t kRasterWaveOffset = 0xD6B5;            // raster_wave_offset
[[maybe_unused]] constexpr uint16_t kRasterWavePhase = 0xD6B6;             // raster_wave_phase
[[maybe_unused]] constexpr uint16_t kRasterWaveSource = 0xD6B7;            // raster_wave_source
[[maybe_unused]] constexpr uint16_t kEnemyGroupCount = 0xD6D4;             // enemy_group_count
constexpr uint16_t kEncounterVariant = 0xD6EE;                             // encounter_variant
constexpr uint16_t kEncounterClasses = 0xD6EF;                             // encounter_disable
constexpr uint16_t kEncounterVariants = 0xD6F2;                            // encounter_variants
constexpr uint16_t kSavedMapChanged = 0xD6F5;                              // saved_map_changed
constexpr uint16_t kBitstreamSource = 0xD6F6;                              // bitstream_source
constexpr uint16_t kBytesLeft = 0xD6F8;                                    // bitstream_bytes_left
constexpr uint16_t kBitsLeft = 0xD6FA;                                     // bitstream_bits_left
constexpr uint16_t kBufferedByte = 0xD6FB;                                 // bitstream_byte
constexpr uint16_t kLzDictionary =
    0xD6FE; // shared field scratch: LZ window, metatile palettes, item-message text
[[maybe_unused]] constexpr uint16_t kIntroProjectileTicks = 0xD6FF;       // intro_projectile_ticks
[[maybe_unused]] constexpr uint16_t kPartyMenuEntryContext = 0xD700;      // intro_projectile_y
[[maybe_unused]] constexpr uint16_t kPartyMenuText = 0xD701;              // party_menu_text
[[maybe_unused]] constexpr uint16_t kIntroProjectileState = 0xD702;       // intro_projectile_state
[[maybe_unused]] constexpr uint16_t kIntroProjectileXOffset = 0xD703;     // intro_projectile_x_offset
[[maybe_unused]] constexpr uint16_t kIntroProjectilePhase = 0xD705;       // intro_projectile_phase
[[maybe_unused]] constexpr uint16_t kIntroProjectileFrame = 0xD706;       // intro_projectile_frame
[[maybe_unused]] constexpr uint16_t kIntroProjectileYOffset = 0xD707;     // intro_projectile_y_offset
[[maybe_unused]] constexpr uint16_t kIntroProjectilePosition = 0xD708;    // intro_projectile_position
[[maybe_unused]] constexpr uint16_t kIntroProjectileOrigin = 0xD70A;      // intro_projectile_origin
[[maybe_unused]] constexpr uint16_t kIntroProjectileStep = 0xD70C;        // intro_projectile_step
[[maybe_unused]] constexpr uint16_t kFormationKind = 0xD70D;              // formation_kind
[[maybe_unused]] constexpr uint16_t kBattleFlags = 0xD70E;                // battle_flags
[[maybe_unused]] constexpr uint16_t kMenuActionIndex = 0xD840;            // menu_action_index
[[maybe_unused]] constexpr uint16_t kMenuSelection = 0xD841;              // race_select_index
[[maybe_unused]] constexpr uint16_t kSaveSlotSelection = 0xD842;          // class_select_index
[[maybe_unused]] constexpr uint16_t kSaveChoice = 0xD843;                 // gender_select_index
[[maybe_unused]] constexpr uint16_t kMenuThirdSelection = 0xD844;         // menu_third_selection
[[maybe_unused]] constexpr uint16_t kMenuFourthSelection = 0xD845;        // menu_fourth_selection
[[maybe_unused]] constexpr uint16_t kMenuFifthSelection = 0xD846;         // menu_fifth_selection
[[maybe_unused]] constexpr uint16_t kMenuSixthSelection = 0xD847;         // menu_sixth_selection
constexpr uint16_t kMenuExitRequested = 0xD84A;                           // menu_exit_requested
constexpr uint16_t kMenuActionResult = 0xD84B;                            // menu_action_result
[[maybe_unused]] constexpr uint16_t kMenuPriceLow = 0xD84C;               // menu_price_low
[[maybe_unused]] constexpr uint16_t kMenuPriceHigh = 0xD84E;              // menu_price_high
[[maybe_unused]] constexpr uint16_t kMenuDescriptionSpriteStart = 0xD850; // menu_description_sprite_start
[[maybe_unused]] constexpr uint16_t kCreationLeader = 0xD852;             // creation_leader
[[maybe_unused]] constexpr uint16_t kCreationRace = 0xD853;               // creation_race
[[maybe_unused]] constexpr uint16_t kNamePage = 0xD854;                   // name_page
[[maybe_unused]] constexpr uint16_t kNameInsertionIndex = 0xD855;         // name_insertion_index
[[maybe_unused]] constexpr uint16_t kNameText = 0xD856;                   // name_text
[[maybe_unused]] constexpr uint16_t kMenuSecondItemBuffer = 0xD857;       // menu_second_item_buffer
[[maybe_unused]] constexpr uint16_t kMenuTransferState = 0xD859;          // menu_transfer_state
[[maybe_unused]] constexpr uint16_t kShopCursorBlink = 0xD85D;            // shop_cursor_blink
[[maybe_unused]] constexpr uint16_t kBattleStatusSuccessCount = 0xDBF3;   // battle_status_success_count
[[maybe_unused]] constexpr uint16_t kItemUseCount = 0xDBF4;               // item_use_count
[[maybe_unused]] constexpr uint16_t kBattleKnockoutMode = 0xDBF6;         // battle_knockout_mode
[[maybe_unused]] constexpr uint16_t kBattleFlashCounter = 0xDBF7;         // battle_flash_counter
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
constexpr uint16_t kEffectDistortion = 0xDCFE;                         // field_effect_distortion
constexpr uint16_t kDialogueCursorPosition = 0xDD28;                   // dialogue_cursor_position
[[maybe_unused]] constexpr uint16_t kFlarePhaseTicks = 0xDD29;         // flare_phase_ticks
constexpr uint16_t kOverflowItemUsesText = 0xDD3E;                     // overflow_item_uses_text
[[maybe_unused]] constexpr uint16_t kEffectTargetMode = 0xDD73;        // effect_target_mode
[[maybe_unused]] constexpr uint16_t kFooterPanelMode = 0xEC83;         // footer_panel_mode
[[maybe_unused]] constexpr uint16_t kGroupDrainTargetCount = 0xEC84;   // group_drain_target_count
[[maybe_unused]] constexpr uint16_t kGroupDrainKnockoutCount = 0xEC85; // group_drain_knockout_count
[[maybe_unused]] constexpr uint16_t kGroupDrainPreviousGroup = 0xEC86; // group_drain_previous_group
void field_callback(Machine &m, uint16_t target) {
    switch (target) {
    case 0xA686:
        animate_map_palette_cycle_core(m);
        break;
    case 0x957E:
        animate_map_tile_strip_core(m);
        break;
    case 0x8391:
        travel_area_exit_core(m);
        break;
    default:
        dispatch_computed_handler_core(m, target);
        break;
    }
}
constexpr uint16_t kMapSegment = 0x1000;
constexpr uint16_t kMapAttributes = 0x2100;

constexpr uint16_t kTravelMoveShiftTable = 0x0900;

void logic_flags(Machine &m, uint16_t value) {
    m.flags() = uint16_t(m.flags() & ~(emu::CF | emu::OF | emu::AF | emu::SF | emu::ZF | emu::PF));
    if (value == 0)
        m.flags() |= emu::ZF;
    if (value & 0x8000)
        m.flags() |= emu::SF;
    if ((std::popcount(uint8_t(value)) & 1) == 0)
        m.flags() |= emu::PF;
}

uint16_t scroll_map_offset(uint16_t scroll) {
    return uint16_t(((scroll >> 8) & 0xF0) * 8 + ((scroll & 0xF0) >> 2));
}

// Core of 0x351893, shared with the four directional strip updates below. Each cell is a 2x2
// tile block; flag 80h duplicates it onto screen 2, otherwise screen 2 receives transparent tile 1FFh.
void draw_map_rectangle_core(Machine &m, uint16_t cells, uint16_t destination, uint16_t size) {
    uint8_t rows = uint8_t(size >> 8);
    do {
        uint16_t cell = cells;
        uint16_t di = destination;
        uint8_t columns = uint8_t(size);
        do {
            uint8_t x = uint8_t(uint8_t(cell) + 8);
            uint8_t y = uint8_t(uint8_t(cell >> 8) + 8);
            uint8_t id = (x < 0x50 && y < 0x50) ? m.rd8(kMapSegment, uint16_t(y * 0x50 + x)) : 0;
            uint16_t si = uint16_t(kMapMetatiles + id * 8);
            uint8_t attributes = 0;
            x = uint8_t(cell);
            y = uint8_t(cell >> 8);
            if (x < 0x40 && y != 0 && y < 0x3F) {
                uint8_t index = m.rd8(kMapSegment, uint16_t(kMapAttributes + y * 0x40 + x));
                attributes = m.rd8(m.ss(), uint16_t(kTileByteFlags + index));
            }
            constexpr uint16_t offsets[] = {0, 2, 0x40, 0x42};
            // Preserve the original write order, including its separate screen-2 fill path.
            if (attributes & 0x80) {
                for (int i = 0; i < 4; ++i) {
                    m.ax() = m.rd16(kMapSegment, uint16_t(si + i * 2));
                    m.wr16(0, uint16_t(di + kScreenMap + offsets[i]), m.ax());
                    m.wr16(0, uint16_t(di + kScreenMap2 + offsets[i]), m.ax());
                }
            } else {
                for (int i = 0; i < 4; ++i) {
                    m.ax() = m.rd16(kMapSegment, uint16_t(si + i * 2));
                    m.wr16(0, uint16_t(di + kScreenMap + offsets[i]), m.ax());
                }
                m.ax() = 0x1FF;
                for (uint16_t offset : offsets)
                    m.wr16(0, uint16_t(di + kScreenMap2 + offset), m.ax());
            }
            if (Machine::field_cell_hook) {
                const int type =
                    x < 64 && y < 64 ? m.rd8(kMapSegment, uint16_t(kMapAttributes + y * 64 + x)) : -1;
                Machine::field_cell_hook(m, uint16_t(di + kScreenMap), id, type, int(int8_t(x)),
                                         int(int8_t(y)));
            }
            m.si() = si;
            di = uint16_t(uint16_t(di + 4) & 0xFFBF);
            cell = uint16_t((cell & 0xFF00) | uint8_t(uint8_t(cell) + 1));
        } while (--columns != 0);
        cells = uint16_t(cells + 0x100);
        destination = uint16_t(uint16_t(destination + 0x80) & 0xF7FF);
    } while (--rows != 0);
    m.cx() &= 0xFF;
    m.bx() = cells;
    m.di() = destination;
}

// Edge updates preserve every register (PUSHA/POPA); their only outputs are screen-map writes.
void draw_map_edge_core(Machine &m, int x, int y, int scroll_x, int scroll_y, uint16_t size) {
    uint16_t saved_ax = m.ax(), saved_bx = m.bx(), saved_si = m.si(), saved_di = m.di(), saved_cx = m.cx();
    uint16_t scroll = m.rd16(m.ss(), kFieldScroll);
    scroll = uint16_t(uint8_t(uint8_t(scroll) + scroll_x) |
                      (uint16_t(uint8_t(uint8_t(scroll >> 8) + scroll_y)) << 8));
    uint16_t cell = m.rd16(m.ss(), kPlayerCell);
    cell = uint16_t(uint8_t(uint8_t(cell) + x) | (uint16_t(uint8_t(uint8_t(cell >> 8) + y)) << 8));
    draw_map_rectangle_core(m, cell, scroll_map_offset(scroll), size);
    m.ax() = saved_ax;
    m.bx() = saved_bx;
    m.si() = saved_si;
    m.di() = saved_di;
    m.cx() = saved_cx;
}
} // namespace

namespace {

constexpr uint16_t kSpriteSegment = 0x9000, kSpritePaletteTable = 0x40B2, kSpriteSegmentTable = 0x3F70;
constexpr uint16_t kCharacterGfxTable = 0x76E8;

constexpr uint16_t kVehicleDescriptors = 0x25F2, kNpcMapTable = 0x4384;

uint8_t read_map_bit_core(Machine &m) {
    uint8_t bits_remaining = uint8_t(m.rd8(m.ss(), kBitsLeft) - 1);
    m.wr8(m.ss(), kBitsLeft, bits_remaining);
    if (bits_remaining == 0) {
        uint16_t left = m.rd16(m.ss(), kBytesLeft);
        if (left == 0) {
            m.ax() = 0;
            return 0;
        }
        m.wr16(m.ss(), kBytesLeft, uint16_t(left - 1));
        m.wr8(m.ss(), kBitsLeft, 8);
        m.wr8(m.ss(), kBufferedByte, m.rd8(m.ds(), m.si()));
        m.si() = uint16_t(m.si() + 1);
    }
    uint8_t buffered = m.rd8(m.ss(), kBufferedByte);
    uint8_t bit = uint8_t(buffered >> 7);
    m.wr8(m.ss(), kBufferedByte, uint8_t(buffered << 1));
    m.ax() = uint16_t(bit * 0x101);
    return bit;
}
uint16_t read_map_bits_core(Machine &m, int count) {
    uint16_t bits = 0;
    for (int i = 0; i < count; ++i)
        bits = uint16_t((bits << 1) | read_map_bit_core(m));
    return bits;
}
void copy_sprite_palette_core(Machine &m, uint16_t sprite, uint16_t color, uint16_t palette) {
    m.flags() = uint16_t(m.flags() & ~emu::DF);
    uint16_t table = uint16_t(sprite * 4);
    uint16_t source = uint16_t(m.rd16(kSpriteSegment, uint16_t(kSpritePaletteTable + table)) + color * 32);
    uint16_t segment = m.rd16(kSpriteSegment, uint16_t(kSpriteSegmentTable + table));
    uint16_t destination = uint16_t(((kPaletteTarget + palette * 32) & 0x3FFF) + kSpriteBuffer);
    for (int i = 0; i < 16; ++i)
        m.wr16(0, uint16_t(destination + i * 2), m.rd16(segment, uint16_t(source + i * 2)));
}
void clear_field_npcs_core(Machine &m) {
    for (uint16_t slot = 0; slot < 16; ++slot) {
        uint16_t record = uint16_t(kNpcTable + slot * 0x14);
        m.wr8(m.ss(), record, 0xFF);
        for (uint16_t byte = 1; byte < 0x14; ++byte)
            if (byte != 0xF && byte != 0x13)
                m.wr8(m.ss(), uint16_t(record + byte), 0);
    }
}
void copy_field_words_core(Machine &m, uint16_t source, uint16_t destination, uint16_t words) {
    int step = (m.flags() & emu::DF) ? -2 : 2;
    for (uint16_t i = 0; i < words; ++i) {
        m.wr16(m.ss(), destination, m.rd16(m.ss(), source));
        source = uint16_t(source + step);
        destination = uint16_t(destination + step);
    }
}
// Spawn the first vehicle parked on this map in the first free NPC record, with palette 14.
void spawn_parked_vehicle_core(Machine &m) {
    uint16_t location = kVehicleLocations;
    int vehicle = 0;
    for (; vehicle < 3; ++vehicle, location = uint16_t(location + 4))
        if (m.rd8(m.ss(), location) == m.rd8(m.ss(), kMapId))
            break;
    if (vehicle == 3)
        return;
    uint16_t record = kNpcTable;
    int slot = 0;
    for (; slot < 16; ++slot, record = uint16_t(record + 0x14))
        if (m.rd8(m.ss(), record) == 0xFF)
            break;
    if (slot == 16)
        return;
    uint16_t descriptor = uint16_t(kVehicleDescriptors + vehicle * 4);
    m.wr8(m.ss(), uint16_t(record + 0xE), 0);
    m.wr16(m.ss(), uint16_t(record + 0xA), 0);
    m.wr16(m.ss(), uint16_t(record + 0xC), 0);
    uint8_t sprite = m.rd8(m.cs(), descriptor);
    m.wr8(m.ss(), uint16_t(record + 1), sprite);
    m.wr8(m.ss(), (kNpcDirectionSpriteSets + 0x6), sprite);
    m.wr8(m.ss(), record, m.rd8(m.cs(), uint16_t(descriptor + 1)));
    uint8_t x = m.rd8(m.ss(), uint16_t(location + 1)), y = m.rd8(m.ss(), uint16_t(location + 2));
    m.wr8(m.ss(), uint16_t(record + 2), x);
    m.wr8(m.ss(), uint16_t(record + 4), x);
    m.wr16(m.ss(), uint16_t(record + 6), uint16_t(x << 5));
    m.wr8(m.ss(), uint16_t(record + 3), y);
    m.wr8(m.ss(), uint16_t(record + 5), y);
    m.wr16(m.ss(), uint16_t(record + 8), uint16_t(y << 5));
    m.wr8(m.ss(), uint16_t(record + 0xF), m.rd8(m.cs(), uint16_t(descriptor + 2)));
    m.wr8(m.ss(), uint16_t(record + 0x10), m.rd8(m.cs(), uint16_t(descriptor + 3)));
    m.wr8(m.ss(), uint16_t(record + 0x11), 0x46);
    copy_sprite_palette_core(m, sprite, 0, 14);
}
} // namespace

// Shared body of ROM 0x350863
// CX is the signed cell delta. Refresh the newly exposed strip and set walking/running speed.
// BX becomes the travel mode; DX retains CX, and AX retains the last speed-test scratch value.
void prepare_player_scroll_core(Machine &m) {
    m.dx() = m.cx();
    if (uint8_t(m.dx()) == 1)
        draw_map_edge_core(m, 8, -4, -24, 0, 0xB01);
    else if (uint8_t(m.dx()) == 0xFF)
        draw_map_edge_core(m, -8, -4, -24, 0, 0xB01);
    else if (uint8_t(m.dx() >> 8) == 1)
        draw_map_edge_core(m, -7, 5, -8, 0x90, 0x110);
    else if (uint8_t(m.dx() >> 8) == 0xFF)
        draw_map_edge_core(m, -7, -5, -8, -16, 0x110);
    m.wr16(m.ss(), kMoveDelta, m.dx());
    m.bx() = m.rd8(m.ss(), kTravelMode);
    m.set_al(m.rd8(m.cs(), uint16_t(kTravelMoveShiftTable + m.bx())));
    m.wr8(m.ss(), kMoveShift, m.al());
    m.set_al(2);
    m.wr8(m.ss(), kAnimationStep, m.al());
    // Keep each AL scratch assignment: early exits leave different register values.
    m.set_al(uint8_t(m.rd8(m.ss(), kOptions) & 1));
    if (m.al()) {
        m.set_al(m.rd8(m.ss(), kScriptActive));
        if (!m.al()) {
            m.set_al(m.rd8(m.ss(), kTravelMode));
            uint8_t mode = m.al();
            // The original runs on foot only (mode 0); the hook may also let a vehicle run (Args a = mode).
            bool vanilla = false;
            if (!mode) {
                m.set_al(m.rd8(m.ss(), kRunningAllowed));
                vanilla = m.al() != 0;
            }
            if (hooks::should(hooks::VB::RunningAllowed, m, vanilla, {mode})) {
                m.set_al(uint8_t(m.rd8(m.ss(), kOptions) & 4));
                bool inverted = m.al() != 0;
                m.ax() = uint16_t(m.rd16(m.ss(), kKeysHeld) & 8);
                if ((m.ax() != 0) == inverted) {
                    m.set_al(1);
                    m.wr8(m.ss(), kMoveShift, 1);
                    m.set_al(4);
                    m.wr8(m.ss(), kAnimationStep, 4);
                }
            }
        }
    }
}

// rom: 0x350863 (near)
void prepare_player_scroll_call_core(Machine &m) { prepare_player_scroll_core(m); }
void prepare_player_scroll(Machine &m) {
    prepare_player_scroll_call_core(m);
    m.ret();
}

// Shared body of ROM 0x350BA3
// Roll a terrain-class encounter using the travel-adjusted range, then choose its weighted battle and
// variant.
void roll_field_encounter_core(Machine &m) {

    // These bank34 helpers have no exported core; read the bank34 random table explicitly because
    // the original reaches it through a far call with CS=4000h, not the field routine's CS=5000h.
    auto random_range_value = [&](uint8_t upper_bound) {
        uint16_t counter = uint16_t(m.rd16(m.ss(), kRngCalls) + 1);
        m.wr16(m.ss(), kRngCalls, counter);
        uint8_t random_index = uint8_t(counter + m.rd16(m.ss(), kFrames));
        // Match bank 34: round(random_byte * upper_bound / 256), so both 0 and the upper endpoint are
        // possible.
        uint16_t product = uint16_t(m.rd8(0x4000, uint16_t(0xBD7 + random_index)) * upper_bound);
        return uint8_t((product >> 8) + ((product & 0x80) ? 1 : 0));
    };
    uint8_t terrain_class = m.al();
    if (m.rd8(m.ss(), kEncountersEnabled) != 0) {
        uint8_t encounter = m.rd8(m.ss(), uint16_t(kEncounterClasses + terrain_class));
        if (encounter != 0xFF) {
            uint16_t encounter_record = uint16_t(0xC3C + encounter * 8);
            uint8_t modifier =
                table_at(m, Table::encounter_modifiers, m.cs(), uint16_t(0xC38 + m.rd8(m.ss(), kTravelMode)));
            // Encounter denominator = table byte + uint8_t((travel modifier - 1)*8), saturated at 255.
            modifier = uint8_t(uint8_t(modifier - 1) << 3);
            uint16_t chance = uint16_t(modifier + table_at(m, Table::encounters, m.cs(), encounter_record));
            // A zero range roll triggers; the second roll scans descending weighted thresholds to select a
            // battle.
            if (random_range_value(chance > 0xFF ? 0xFF : uint8_t(chance)) == 0 &&
                hooks::should(hooks::VB::RandomEncounter, m, true, {terrain_class})) {
                uint8_t roll = random_range_value(0xFF);
                uint16_t weight_threshold = 0xC30;
                do {
                    weight_threshold = uint16_t(weight_threshold + 1);
                    encounter_record = uint16_t(encounter_record + 1);
                } while (roll < table_at(m, Table::encounter_thresholds, m.cs(), weight_threshold));
                m.wr8(m.ss(), kFieldEventArgument, table_at(m, Table::encounters, m.cs(), encounter_record));
                m.wr8(m.ss(), kFieldEventKind, 8);
                m.wr8(m.ss(), kFieldEventPending, 1);
                uint8_t variant = m.rd8(m.ss(), uint16_t(kEncounterVariants + terrain_class));
                m.bx() = variant;
                m.wr8(m.ss(), kEncounterVariant, variant);
            }
        }
    }
    return;
}

// rom: 0x350BA3 (near)
void roll_field_encounter(Machine &m) {
    roll_field_encounter_core(m);
    m.ret();
}

// rom: 0x35185C (near)
// Read eight packed-map bits into DL, retaining DH. AX contains the last bit in both bytes.
// The existing bit reader core has private linkage in bank35_field.cpp (outside this task's territory).
void bitstream_read_byte(Machine &m) {
    uint8_t byte = uint8_t(read_map_bits_core(m, 8));
    m.dx() = uint16_t((m.dx() & 0xFF00) | byte);
    m.ret();
}

// rom: 0x351893 (near)
// BX packed map cell, DI circular screen-map offset, CL/CH width/height (zero means 256).
// CL and segments are preserved; CH ends zero, BX/DI advance by rows, AX/SI describe the last cell.
void draw_map_rectangle(Machine &m) {
    draw_map_rectangle_core(m, m.bx(), m.di(), m.cx());
    m.ret();
}

// rom: 0x35194D (near)
// AX packed pixel scroll -> DI circular screen-map address; AX/BX preserved.
void scroll_to_map_offset(Machine &m) {
    m.di() = scroll_map_offset(m.ax());
    m.ret();
}

// Shared body of ROM 0x351963
// Refresh the 16-cell row entering at the upper edge of the scrolling viewport; registers preserved.
void draw_map_top_edge_core(Machine &m) {
    draw_map_edge_core(m, -7, -5, -8, -16, 0x110);
    return;
}

// rom: 0x351963
void draw_map_top_edge(Machine &m) {
    draw_map_top_edge_core(m);
    m.retf();
}

// rom: 0x3519AF
// Refresh the 11-cell column entering at the left edge; registers preserved.
void draw_map_left_edge_call_core(Machine &m) { draw_map_edge_core(m, -8, -4, -24, 0, 0xB01); }
void draw_map_left_edge(Machine &m) {
    draw_map_left_edge_call_core(m);
    m.retf();
}

// Shared body of ROM 0x3519D2
// Refresh the 11-cell column entering at the right edge; registers preserved.
void draw_map_right_edge_core(Machine &m) {
    draw_map_edge_core(m, 8, -4, -24, 0, 0xB01);
    return;
}

// rom: 0x3519D2
void draw_map_right_edge(Machine &m) {
    draw_map_right_edge_core(m);
    m.retf();
}

// Shared body of ROM 0x352490
// Reset 16 NPC records, marking unused slots FFh. Bytes +0Fh and +13h are deliberately retained.

// rom: 0x352490
void clear_field_npcs_call_core(Machine &m) { clear_field_npcs_core(m); }
void clear_field_npcs(Machine &m) {
    clear_field_npcs_call_core(m);
    m.retf();
}

// BX packed cell -> AX matched NPC address or zero, also stored to npc_last_match. ZF is the result.
// Both destination and departure cells count as occupied; BP/CX are preserved.
void find_npc_at_cell_core(Machine &m) {
    uint16_t match = 0;
    for (uint16_t slot = 0; slot < 16; ++slot) {
        uint16_t record = uint16_t(kNpcTable + slot * 0x14);
        if (m.rd8(m.ss(), record) != 0xFF && (m.bx() == m.rd16(m.ss(), uint16_t(record + 2)) ||
                                              m.bx() == m.rd16(m.ss(), uint16_t(record + 4)))) {
            match = record;
            break;
        }
    }
    m.ax() = match;
    m.wr16(m.ss(), kNpcLastMatch, match);
    logic_flags(m, match);
}
// Shared body of ROM 0x35293D
// BX packed cell -> AX matched NPC address or zero, also stored to npc_last_match. ZF is the result.

// rom: 0x35293D
void find_npc_at_cell_call_core(Machine &m) { find_npc_at_cell_core(m); }
void find_npc_at_cell(Machine &m) {
    find_npc_at_cell_call_core(m);
    m.retf();
}

// Shared body of ROM 0x357E03
// AL progress index -> AX nibble (even indices select high nibble), BX preserved.
void read_packed_progress_core(Machine &m) {
    uint8_t counter_index = m.al();
    uint8_t packed_counters = m.rd8(m.ss(), uint16_t(kPackedProgress + (counter_index >> 1)));
    m.ax() = uint16_t(((counter_index & 1) ? packed_counters : (packed_counters >> 4)) & 0xF);
    return;
}

// rom: 0x357E03
void read_packed_progress(Machine &m) {
    read_packed_progress_core(m);
    m.retf();
}

// rom: 0x350032
// Reset field working RAM (255 words at 0000:D500, honoring DF), then set scroll/animation defaults.
// AX=8, CX=0, DI is past the fill, DS=ES=0; other registers and DF retained.
void reset_field_state_core(Machine &m) {
    int step = (m.flags() & emu::DF) ? -2 : 2;
    uint16_t di = kPlayerWalkToggle;
    for (int i = 0; i < 0xFF; ++i, di = uint16_t(di + step))
        m.wr16(0, di, 0);
    constexpr uint16_t bytes[] = {kOamCount,
                                  kMoveShift,
                                  kBlinkToggleA,
                                  kPlayerBlinkToggleB,
                                  kFieldPaletteFirstFrame,
                                  kFieldPaletteSecondFrame,
                                  kEncounterVariant,
                                  kScriptActive,
                                  kPlayerStepped,
                                  kPlayerWalkToggle};
    for (uint16_t off : bytes)
        m.wr8(m.ss(), off, 0);
    constexpr uint16_t words[] = {
        kMoveDelta, kCamX, kCamY, kFieldPreSpriteCallback, kFieldPostSpriteCallback, kFieldNpcCallback};
    for (uint16_t off : words)
        m.wr16(m.ss(), off, 0);
    m.wr8(m.ss(), kAnimationStep, 1);
    for (uint16_t off : {int(kFieldPaletteTimer), int(kFieldPaletteSecondTimer), int(kPlayerBlinkTimerA),
                         int(kPlayerBlinkTimerB)})
        m.wr8(m.ss(), off, 0x20);
    m.wr16(m.ss(), kFieldScroll, 8);
    m.wr16(m.ss(), uint16_t(kFieldScroll + 2), 8);
    m.ax() = 8;
    m.cx() = 0;
    m.di() = di;
    m.ds() = m.es() = 0;
}
void reset_field_state(Machine &m) {
    reset_field_state_core(m);
    m.retf();
}

// Shared body of ROM 0x35013B
void clear_field_overlay_core(Machine &m) {
    hooks::emit(hooks::Event::Dialogue, m, {0});

    m.wr8(m.ss(), kOverlayEnabled, 0);
    m.out8(0xB2, uint8_t(m.in8(0xB2) & 0xEF));
    for (int i = 0; i < 0x400; ++i)
        m.wr16(0, uint16_t(kOverlayScreen + i * 2), 0x1FF);
    return;
}

// rom: 0x35013B
// Disable scanline IRQ bit 10h and fill the third screen-map page with transparent tile 1FFh.
// Registers and direction flag preserved.
void clear_field_overlay(Machine &m) {
    clear_field_overlay_core(m);
    m.retf();
}

// Shared body of ROM 0x350295
// Initialize the player sprite and copy its palette (party leader on foot, mode-specific vehicle otherwise).
// Preserves registers, clears DF through copy_palettes. Palette table indexing wraps BL*4 as a byte.
void initialize_player_sprite_core(Machine &m) {
    constexpr uint16_t kFacingTable = 0x0324, kVehicleSprites = 0x0321;

    m.wr16(m.ss(), kOamBaseY, 0x68);
    m.wr16(m.ss(), kOamBaseX, 0x40);
    m.wr8(m.ss(), kPlayerSpriteFacing, m.rd8(m.cs(), uint16_t(kFacingTable + m.rd8(m.ss(), kPlayerFacing))));
    m.wr8(m.ss(), kBlinkToggleA, 0);
    m.wr8(m.ss(), kPlayerBlinkTimerA, 0x20);
    uint8_t mode = m.rd8(m.ss(), kTravelMode);
    uint8_t sprite, color = 0;
    if (mode == 0) {
        uint16_t graphics = table_at16(m, Table::monster_type_graphics, kSpriteSegment,
                                       uint16_t(kCharacterGfxTable + m.rd8(m.ss(), kMemberType) * 2));
        sprite = uint8_t(graphics);
        color = uint8_t(graphics >> 8);
    } else {
        sprite = m.rd8(m.cs(), uint16_t(kVehicleSprites + uint8_t(mode - 1)));
    }
    m.wr8(m.ss(), kSpriteSetIndex, sprite);
    copy_sprite_palette_core(m, uint16_t(sprite & 0x3F), color, 8);
}

// Shared body of ROM 0x350295

// rom: 0x350295
void initialize_player_sprite(Machine &m) {
    initialize_player_sprite_core(m);
    m.retf();
}

// Shared body of ROM 0x3503E9
// Clear 256 words at SS:DI, preserving registers and clearing DF.
void clear_field_word_buffer_core(Machine &m) {
    m.flags() = uint16_t(m.flags() & ~emu::DF);
    for (int i = 0; i < 0x100; ++i)
        m.wr16(m.ss(), uint16_t(m.di() + i * 2), 0);
    return;
}

// rom: 0x3503E9
void clear_field_word_buffer(Machine &m) {
    clear_field_word_buffer_core(m);
    m.retf();
}

// Shared body of ROM 0x35048F
// Save 0x800 bytes of field OAM/screen data to SS:3800, honoring DF; mark saved field unchanged.
void backup_field_display_core(Machine &m) {
    copy_field_words_core(m, kSpriteBuffer, kDisplayBackup, 0x400);
    m.wr8(m.ss(), kSavedMapChanged, 0);
    return;
}

// rom: 0x35048F
void backup_field_display(Machine &m) {
    backup_field_display_core(m);
    m.retf();
}

// rom: 0x351797 (near)
// Packed-map LZ decoder: one-bit literal marker, 8-bit literals, or 12-bit dictionary offset and
// 4-bit length+2. Zero offset terminates. Dictionary reads/writes use SS (ROM has an SS override missing from
// asm/bank_35.s). All registers preserved; bitstream state and DS/ES memory receive the outputs.
void decompress_field_map_core(Machine &m) {
    uint16_t saved_si = m.si(), saved_ax = m.ax();
    m.wr16(m.ss(), kBitstreamSource, m.si());
    m.wr16(m.ss(), kBytesLeft, m.cx());
    m.wr8(m.ss(), kBitsLeft, 1);
    uint16_t cursor = 1, destination = m.di();
    for (;;) {
        if (read_map_bit_core(m)) {
            uint8_t literal = uint8_t(read_map_bits_core(m, 8));
            m.wr8(m.es(), destination, literal);
            m.wr8(m.ss(), uint16_t(kLzDictionary + cursor), literal);
            destination = uint16_t(destination + 1);
            cursor = uint16_t((cursor + 1) & 0xFFF);
        } else {
            uint16_t source = read_map_bits_core(m, 12);
            if (source == 0)
                break;
            uint16_t count = uint16_t(read_map_bits_core(m, 4) + 2);
            for (uint16_t i = 0; i < count; ++i) {
                uint8_t value = m.rd8(m.ss(), uint16_t(kLzDictionary + source));
                m.wr8(m.es(), destination, value);
                m.wr8(m.ss(), uint16_t(kLzDictionary + cursor), value);
                destination = uint16_t(destination + 1);
                source = uint16_t((source + 1) & 0xFFF);
                cursor = uint16_t((cursor + 1) & 0xFFF);
            }
        }
    }
    m.si() = saved_si;
    m.ax() = saved_ax;
}
void decompress_field_map(Machine &m) {
    decompress_field_map_core(m);
    m.ret();
}

// rom: 0x351989
// Refresh the 16-cell row entering at the bottom edge, preserving registers.
void draw_map_bottom_edge_call_core(Machine &m) { draw_map_edge_core(m, -7, 5, -8, 0x90, 0x110); }
void draw_map_bottom_edge(Machine &m) {
    draw_map_bottom_edge_call_core(m);
    m.retf();
}

// Shared body of ROM 0x3519F5
// Draw the full 16-by-11-cell viewport around the player, preserving registers.
void draw_map_viewport_core(Machine &m) { draw_map_edge_core(m, -7, -4, -8, 0, 0xB10); }

// Shared body of ROM 0x3519F5

// rom: 0x3519F5
void draw_map_viewport(Machine &m) {
    draw_map_viewport_core(m);
    m.retf();
}

// Shared body of ROM 0x3524F3
// Copy all NPC records to the field backup buffer, honoring DF. Registers preserved.
void backup_field_npcs_core(Machine &m) {
    copy_field_words_core(m, kNpcTable, kNpcBackup, 0xA0);
    return;
}

// rom: 0x3524F3
void backup_field_npcs(Machine &m) {
    backup_field_npcs_core(m);
    m.retf();
}

// Shared body of ROM 0x35250D
// Restore all NPC records from the field backup buffer, honoring DF. Registers preserved.
void restore_field_npcs_core(Machine &m) {
    copy_field_words_core(m, kNpcBackup, kNpcTable, 0xA0);
    return;
}

// rom: 0x35250D
void restore_field_npcs(Machine &m) {
    restore_field_npcs_core(m);
    m.retf();
}

// rom: 0x352527
// Spawn the first parked vehicle matching this map into the first free NPC slot, then load palette 14.
// Registers preserved; if a vehicle is spawned, copy_palettes clears DF.
void spawn_parked_vehicle_call_core(Machine &m) { spawn_parked_vehicle_core(m); }
void spawn_parked_vehicle(Machine &m) {
    spawn_parked_vehicle_call_core(m);
    m.retf();
}

// Shared body of ROM 0x3525FE
// Load six NPC palettes and the map's progress-gated NPC descriptors, then spawn a parked vehicle.
// Special FE records select sprite visibility from packed event bits. Registers preserved.
void initialize_map_npcs_core(Machine &m) {

    clear_field_npcs_core(m);
    uint16_t descriptor = m.rd16(m.cs(), uint16_t(kNpcMapTable + m.rd8(m.ss(), kMapId) * 2));
    if (descriptor != 0) {
        for (int palette = 0; palette < 6; ++palette) {
            uint8_t sprite = m.rd8(m.cs(), descriptor), color = m.rd8(m.cs(), uint16_t(descriptor + 1));
            if (sprite >= 0xF0) {
                uint16_t type = m.rd8(m.ss(), uint16_t(kMemberType + (sprite & 3) * 0x1F));
                uint16_t graphics = table_at16(m, Table::monster_type_graphics, kSpriteSegment,
                                               uint16_t(kCharacterGfxTable + type * 2));
                sprite = uint8_t(graphics);
                color = uint8_t(graphics >> 8);
            }
            m.wr8(m.ss(), uint16_t(kNpcDirectionSpriteSets + palette + 1), sprite);
            copy_sprite_palette_core(m, sprite, color, uint16_t(palette + 9));
            descriptor = uint16_t(descriptor + 2);
        }
        uint16_t record = kNpcTable;
        for (int slot = 0; slot < 16;) {
            m.wr8(m.ss(), uint16_t(record + 0xE), 0);
            m.wr16(m.ss(), uint16_t(record + 0xA), 0);
            m.wr16(m.ss(), uint16_t(record + 0xC), 0);
            uint8_t gate = m.rd8(m.cs(), descriptor);
            if (gate == 0xFF)
                break;
            if (gate == 0xFE) {
                uint8_t event = m.rd8(m.cs(), uint16_t(descriptor + 1));
                uint8_t bit =
                    uint8_t((m.rd8(m.ss(), uint16_t(kBestiaryFlags + (event >> 3))) >> (event & 7)) & 1);
                m.wr8(m.ss(), record, bit);
                uint8_t event_kind = uint8_t(event + 0x10);
                m.wr8(m.ss(), uint16_t(record + 0xF), event_kind);
                m.wr8(m.ss(), uint16_t(record + 1), 0x48);
                m.wr8(m.ss(), kVehicleSpriteSet, 0x48);
                uint8_t flags = m.rd8(m.cs(), uint16_t(descriptor + 7));
                m.wr8(m.ss(), uint16_t(record + 0x11), uint8_t((flags & 0xF8) | 7));
            } else {
                uint8_t packed = m.rd8(m.ss(), uint16_t(kPackedProgress + (gate >> 1)));
                uint8_t progress = uint8_t((((gate & 1) ? packed : (packed >> 4)) & 0xF) + 1);
                uint8_t bounds = m.rd8(m.cs(), uint16_t(descriptor + 1));
                if ((bounds & 0xF) < progress || (bounds >> 4) >= progress) {
                    descriptor = uint16_t(descriptor + 8);
                    continue;
                }
                uint8_t flags = m.rd8(m.cs(), uint16_t(descriptor + 7));
                uint8_t interaction = m.rd8(m.cs(), uint16_t(descriptor + 5));
                if ((interaction & 0xF0) == 0xF0 &&
                    m.rd8(m.ss(), uint16_t(kGameState + (interaction & 3) * 0x1F)) == 0xFF)
                    flags |= 8;
                m.wr8(m.ss(), uint16_t(record + 0x11), flags);
                m.wr8(m.ss(), record, m.rd8(m.cs(), uint16_t(descriptor + 2)));
                m.wr8(m.ss(), uint16_t(record + 0xF), interaction);
                uint8_t set = uint8_t(m.rd8(m.cs(), uint16_t(descriptor + 7)) & 7);
                m.wr8(m.ss(), uint16_t(record + 1),
                      set ? m.rd8(m.ss(), uint16_t(kNpcDirectionSpriteSets + set)) : 0xFF);
            }
            uint8_t x = m.rd8(m.cs(), uint16_t(descriptor + 3)), y = m.rd8(m.cs(), uint16_t(descriptor + 4));
            m.wr8(m.ss(), uint16_t(record + 2), x);
            m.wr8(m.ss(), uint16_t(record + 4), x);
            m.wr16(m.ss(), uint16_t(record + 6), uint16_t(x << 5));
            m.wr8(m.ss(), uint16_t(record + 3), y);
            m.wr8(m.ss(), uint16_t(record + 5), y);
            m.wr16(m.ss(), uint16_t(record + 8), uint16_t(y << 5));
            m.wr8(m.ss(), uint16_t(record + 0x10), m.rd8(m.cs(), uint16_t(descriptor + 6)));
            ++slot;
            record = uint16_t(record + 0x14);
            descriptor = uint16_t(descriptor + 8);
        }
    }
    spawn_parked_vehicle_core(m);
}

// Shared body of ROM 0x3525FE

// rom: 0x3525FE
void initialize_map_npcs(Machine &m) {
    initialize_map_npcs_core(m);
    m.retf();
}

// Shared body of ROM 0x354530
// Clear the two field interaction bytes; AX ends zero, other registers retained.
void clear_field_interaction_core(Machine &m) {

    m.ax() = 0;
    m.wr8(m.ss(), kInteractionState, 0);
    m.wr8(m.ss(), uint16_t(kInteractionState + 1), 0);
    return;
}

// rom: 0x354530 (near)
void clear_field_interaction(Machine &m) {
    clear_field_interaction_core(m);
    m.ret();
}

// Shared body of ROM 0x3580E3
// Clear queued field event state, preserving registers.
void clear_field_event_core(Machine &m) {

    for (int i = 0; i < 3; ++i)
        m.wr8(m.ss(), uint16_t(kFieldEventPending + i), 0);
    return;
}

// rom: 0x3580E3
void clear_field_event(Machine &m) {
    clear_field_event_core(m);
    m.retf();
}

namespace {

constexpr uint16_t kNpcFacingTable = 0x282B;
constexpr uint16_t kAdjacentCellDeltaTable = 0x0721;
void cmp_item_byte(Machine &m, uint8_t item_id, uint8_t wanted_item) {
    const auto result = uint8_t(item_id - wanted_item);
    logic_flags(m, result); // replace SF's word interpretation below for byte comparisons
    if (result & 0x80)
        m.flags() |= emu::SF;
    if (item_id < wanted_item)
        m.flags() |= emu::CF;
    if ((item_id ^ wanted_item) & (item_id ^ result) & 0x80)
        m.flags() |= emu::OF;
    if ((item_id ^ wanted_item ^ result) & 16)
        m.flags() |= emu::AF;
}
// Scan CX two-byte item records, ignoring AH-marked empty slots; ZF reports an AL match.
void scan_item_records_core(Machine &m) {
    do {
        const auto count = m.rd8(m.ss(), uint16_t(m.si() + 1));
        cmp_item_byte(m, m.ah(), count);
        if (m.ah() != count) {
            const auto item = m.rd8(m.ss(), m.si());
            cmp_item_byte(m, m.al(), item);
            if (m.al() == item)
                return;
        }
        m.si() = uint16_t(m.si() + 2);
        --m.cx();
    } while (m.cx() != 0);
    logic_flags(m, m.ah());
    if (m.ah() & 0x80)
        m.flags() |= emu::SF;
}
// Find AL among four members' equipment, inventory and key items; return its address in SI with ZF set.
void find_party_item_core(Machine &m) {
    const auto ax = m.ax(), cx = m.cx();
    m.set_ah(255);
    constexpr uint16_t sources[] = {kMember0Equipment,
                                    (kMember0Equipment + 0x1F),
                                    (kMember0Equipment + 0x3E),
                                    (kMember0Equipment + 0x5D),
                                    kInventory,
                                    kKeyItems};
    for (unsigned i = 0; i < 6; ++i) {
        m.si() = sources[i];
        m.cx() = i == 5 ? 9 : 8;
        scan_item_records_core(m);
        if (m.flags() & emu::ZF)
            break;
    }
    m.ax() = ax;
    m.cx() = cx;
}
// The far lookup at 340BC9 uses bank 34's own table and preserves BX. No registered implementation exists.
void lookup_wave_value_core(Machine &m) { m.ax() = m.rd16(0x4000, uint16_t(0x0cd7 + uint8_t(m.ax()) * 2)); }
} // namespace
// Shared body of ROM 0x35BC21
// Fill 144 scanline distortion bytes from the frame-indexed signed wave table and AX's amplitude.
// All registers preserved; each frame-counter read occurs anew, matching the original memory order.
void build_field_distortion_core(Machine &m) {
    const auto ax = m.ax();
    for (unsigned count = 144, index = 0; count; --count, ++index) {
        const auto frame = m.rd16(m.ss(), kFrames);
        m.ax() = uint16_t((count * 2 + frame) * 4);
        lookup_wave_value_core(m);
        const int32_t product = int32_t(int16_t(m.ax())) * int16_t(ax);
        m.wr8(m.ss(), uint16_t(kEffectDistortion + index), uint8_t(uint32_t(product) >> 8));
    }
    m.ax() = ax;
    return;
}

// rom: 0x35BC21 (near)
void build_field_distortion(Machine &m) {
    build_field_distortion_core(m);
    m.ret();
}
// Shared body of ROM 0x357E1E
// Set a packed-progress nibble. Even indices shift DL four bits, odd indices mask DL; AH retains the id.
void write_packed_progress_core(Machine &m) {
    const auto id = m.al();
    m.set_ah(id);
    const auto offset = uint16_t(kPackedProgress + (id >> 1));
    m.set_al(m.rd8(m.ss(), offset));
    uint8_t counter_value = uint8_t(m.dx());
    if (id & 1) {
        counter_value &= 15;
        m.set_al(m.al() & 0xf0);
    } else {
        counter_value = uint8_t(counter_value << 4);
        m.set_al(m.al() & 15);
    }
    m.dx() = uint16_t((m.dx() & 0xff00) | counter_value);
    // ROM 357E3D ends in OR AL,DL; the destination-selection caller exposes these byte flags.
    m.set_al(computed::logic<uint8_t>(m, uint8_t(m.al() | counter_value)));
    m.wr8(m.ss(), offset, m.al());
    return;
}

// rom: 0x357E1E
void write_packed_progress(Machine &m) {
    write_packed_progress_core(m);
    m.retf();
}
// rom: 0x357DD1 (near)
// Read AL's bestiary bit at D0BB, returning AL=0/1 while retaining AH/BX/CX.
void read_bestiary_bit_call_core(Machine &m) {
    const auto id = m.al();
    m.set_al(uint8_t((m.rd8(m.ss(), uint16_t(kBestiaryFlags + (id >> 3))) >> (id & 7)) & 1));
    (void)computed::logic<uint8_t>(m, m.al());
}
void read_bestiary_bit(Machine &m) {
    read_bestiary_bit_call_core(m);
    m.ret();
}
// rom: 0x357DBD (near)
// Find item AL among CX item/count pairs at SS:SI, ignoring records with count AH. ZF signals a match.
void scan_item_records(Machine &m) {
    scan_item_records_core(m);
    m.ret();
}
// Shared body of ROM 0x357D76
// Search four members' equipment, inventory and key items for AL. Return SI and exact flags; AX/CX saved.

// rom: 0x357D76 (near)
void find_party_item(Machine &m) {
    find_party_item_core(m);
    m.ret();
}
// Shared body of ROM 0x3524E5
// Return the AL low-nibble NPC record pointer in SI; preserve AX.
void npc_record_pointer_core(Machine &m) {
    m.si() = uint16_t(kNpcTable + (m.al() & 15) * 20);
    return;
}

// rom: 0x3524E5
void npc_record_pointer(Machine &m) {
    npc_record_pointer_core(m);
    m.retf();
}
// Shared body of ROM 0x3527F0
// Face the matched visible NPC toward the player. Hidden type-four records redirect to the next record.
// All registers preserved; BP addressing uses SS even when DS differs.
void face_interacting_npc_core(Machine &m) {
    uint16_t npc = m.rd16(m.ss(), kNpcLastMatch);
    if (npc) {
        bool usable = true;
        if (m.rd8(m.ss(), uint16_t(npc + 17)) & 8) {
            if (m.rd8(m.ss(), uint16_t(npc + 15)) != 4)
                usable = false;
            else
                npc = uint16_t(npc + 20);
        }
        if (usable && (m.rd8(m.ss(), npc) & 0x40)) {
            const auto facing = m.rd8(m.cs(), uint16_t(kNpcFacingTable + m.rd8(m.ss(), kPlayerFacing)));
            m.wr8(m.ss(), npc, facing);
        }
    }
    return;
}

// rom: 0x3527F0
void face_interacting_npc(Machine &m) {
    face_interacting_npc_core(m);
    m.retf();
}
// Shared body of ROM 0x3506E9
// Test whether the adjacent cell is occupied by an NPC or out of map bounds. ZF set means clear.
// All registers preserved; only npc_last_match and exact arithmetic flags can change.
void test_adjacent_npc_cell_core(Machine &m) {
    const auto ax = m.ax(), bx = m.bx();
    const uint16_t table_index = uint8_t(m.rd8(m.ss(), kPlayerFacing) * 2);
    const auto delta = m.rd16(m.cs(), uint16_t(kAdjacentCellDeltaTable + table_index));
    const auto player = m.rd16(m.ss(), kPlayerCell);
    m.bx() = uint16_t(uint8_t(uint8_t(delta) + uint8_t(player)) |
                      (uint16_t(uint8_t(uint8_t(delta >> 8) + uint8_t(player >> 8))) << 8));
    if (uint8_t(m.bx()) < 64 && (m.bx() >> 8) < 64) {
        find_npc_at_cell_core(m);
        if (m.flags() & emu::ZF)
            logic_flags(m, 0);
        else {
            logic_flags(m, 0x00ff);
            m.flags() |= emu::SF;
        }
    } else {
        logic_flags(m, 0x00ff);
        m.flags() |= emu::SF;
    }
    m.ax() = ax;
    m.bx() = bx;
    return;
}

// rom: 0x3506E9
void test_adjacent_npc_cell(Machine &m) {
    test_adjacent_npc_cell_core(m);
    m.retf();
}

namespace {

struct FrameRegisters {
    uint16_t ax, bx, cx, dx, bp, si, di, ds, es, ss;
    explicit FrameRegisters(Machine &m)
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
void frame_out16(Machine &m, uint8_t port, uint16_t value) {
    m.out8(port, uint8_t(value));
    m.out8(uint8_t(port + 1), uint8_t(value >> 8));
}
} // namespace

// Shared body of ROM 0x3505A6
// Build a field frame, invoke the map callbacks, then wait for its VBlank; all registers preserved.
void update_field_frame_core(Machine &m) {
    const FrameRegisters saved(m);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xc000;
        run_field_pre_sprite_callback_core(m);
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        draw_player_sprite_core(m);
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xc000;
        run_field_npc_callback_core(m);
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        advance_and_draw_npcs_core(m);
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xc000;
        run_field_post_sprite_callback_core(m);
        m.cs() = caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        advance_player_animation_timers_core(m);
        m.cs() = caller_cs;
    }
    update_map_frame_effect_core(m);
    m.wr8(m.ss(), kFieldFrameWaiting, 1);
    m.call_far(0x4000, 0x0009);
    m.wr8(m.ss(), kFieldFrameWaiting, 0);
    hooks::emit(hooks::Event::PresentationPicture, m, {2});
    saved.restore(m);
    return;
}

// rom: 0x3505A6
void update_field_frame(Machine &m) {
    update_field_frame_core(m);
    m.retf();
}
// Shared body of ROM 0x35A2D4
// Invoke this map's per-frame effect unless fading, preserving registers and the mapped asset bank.
void update_map_frame_effect_core(Machine &m) {
    const FrameRegisters saved(m);
    uint8_t bank = m.in8(0xC2);
    m.set_al(bank);
    m.bx() = uint16_t((m.bx() & 0xFF00) | m.rd8(m.ss(), kFieldFadeActive));
    if (uint8_t(m.bx()) == 0) {
        m.bx() = uint16_t(m.rd8(m.ss(), kMapId) * 2 + 0xA302);
        m.bx() = m.rd16(m.cs(), m.bx());
        if (m.bx())
            field_callback(m, m.bx());
    }
    m.out8(0xC2, bank);
    saved.restore(m);
    return;
}

// rom: 0x35A2D4 (near)
void update_map_frame_effect(Machine &m) {
    update_map_frame_effect_core(m);
    m.ret();
}
// Shared body of ROM 0x3593B0
// Invoke the map's display callback and restore its mapped asset bank and all registers.
void update_map_display_effect_core(Machine &m) {
    const FrameRegisters saved(m);
    uint8_t bank = m.in8(0xC2);
    m.set_al(bank);
    m.bx() = uint16_t(m.rd8(m.ss(), kMapId) * 2 + 0x93D5);
    m.bx() = m.rd16(m.cs(), m.bx());
    if (m.bx())
        field_callback(m, m.bx());
    m.out8(0xC2, bank);
    saved.restore(m);
    return;
}

// rom: 0x3593B0 (near)
void update_map_display_effect(Machine &m) {
    update_map_display_effect_core(m);
    m.ret();
}
// rom: 0x3505E1
// Upload field display/scroll registers and palettes, then invoke the map-specific display callback.
void upload_field_display(Machine &m) {
    m.set_al(m.rd8(m.ss(), kFieldOverlayMode));
    bool overlay = m.al() != 0;
    m.ax() = overlay ? 0xF137 : 0xF107;
    frame_out16(m, 0, m.ax());
    m.set_al(overlay ? 0x52 : 0x32);
    m.out8(7, m.al());
    m.ax() = m.rd16(m.ss(), kFieldScroll);
    frame_out16(m, 0x10, m.ax());
    m.ax() = overlay ? 0 : m.rd16(m.ss(), uint16_t(kFieldScroll + 2));
    frame_out16(m, 0x12, m.ax());
    if (overlay) {
        m.ax() = m.rd16(m.ss(), kFieldWindowTopLeft);
        frame_out16(m, 8, m.ax());
        m.ax() = m.rd16(m.ss(), kFieldWindowBottomRight);
        frame_out16(m, 0xA, m.ax());
    }
    dma_palettes_core(m);
    update_map_display_effect_core(m);
    m.retf();
}
// rom: 0x35062F (near)
// Choose the first held direction in the original up/down/left/right priority order.
void process_field_direction_entry_core(Machine &m) {
    m.ax() = m.rd16(m.ss(), kKeysHeld);
    if (m.ax() & 0xF0) {
        m.bx() = (m.ax() & 0x10) ? 1 : (m.ax() & 0x40) ? 0 : (m.ax() & 0x80) ? 2 : 3;
        attempt_player_step_core(m);
    } else {
        m.set_al(1);
        m.wr8(m.ss(), kAnimationStep, 1);
    }
    return;
}
void process_field_direction(Machine &m) {
    process_field_direction_entry_core(m);
    m.ret();
}
// Shared body of ROM 0x350941
// Animate signed pixel steps until both scroll coordinates align with the 16-pixel cell grid.
void animate_player_scroll_core(Machine &m) {
    do {
        m.set_al(uint8_t(m.dx()));
        m.ax() = uint16_t(int16_t(int8_t(m.al())));
        m.wr16(m.ss(), kCamX, uint16_t(m.rd16(m.ss(), kCamX) + m.ax()));
        m.set_al(uint8_t(m.dx() >> 8));
        m.ax() = uint16_t(int16_t(int8_t(m.al())));
        m.wr16(m.ss(), kCamY, uint16_t(m.rd16(m.ss(), kCamY) + m.ax()));
        uint16_t scroll = m.rd16(m.ss(), kFieldScroll);
        m.ax() = uint16_t(uint8_t(uint8_t(scroll) + uint8_t(m.dx())) |
                          (uint16_t(uint8_t(uint8_t(scroll >> 8) + uint8_t(m.dx() >> 8))) << 8));
        m.wr16(m.ss(), kFieldScroll, m.ax());
        m.wr16(m.ss(), uint16_t(kFieldScroll + 2), m.ax());
        m.call_far(0x4000, 0x5c);
        update_field_frame_core(m);
        m.ax() = m.rd16(m.ss(), kFieldScroll);
        m.set_al(uint8_t(m.al() - 8));
        m.ax() &= 0x0F0F;
    } while (m.ax());
    return;
}

// rom: 0x350941 (near)
void animate_player_scroll(Machine &m) {
    animate_player_scroll_core(m);
    m.ret();
}
// Shared body of ROM 0x350904
// Finish a pending cell move, or draw an idle field frame when no movement is queued.
void finish_player_move_core(Machine &m) {
    m.dx() = m.rd16(m.ss(), kMoveDelta);
    if (m.dx()) {
        m.cx() = uint16_t((m.cx() & 0xFF00) | m.rd8(m.ss(), kMoveShift));
        unsigned shift = uint8_t(m.cx()) & 31;
        m.dx() = uint16_t((shift < 8 ? uint8_t(uint8_t(m.dx()) << shift) : 0) |
                          (uint16_t(shift < 8 ? uint8_t(uint8_t(m.dx() >> 8) << shift) : 0) << 8));
        animate_player_scroll_core(m);
        uint16_t delta = m.rd16(m.ss(), kMoveDelta), cell = m.rd16(m.ss(), kPlayerCell);
        m.ax() = uint16_t(uint8_t(uint8_t(delta) + uint8_t(cell)) |
                          (uint16_t(uint8_t(uint8_t(delta >> 8) + uint8_t(cell >> 8))) << 8));
        m.wr16(m.ss(), kPlayerCell, m.ax());
        m.ax() = 0;
        m.wr16(m.ss(), kMoveDelta, 0);
        m.wr8(m.ss(), kPlayerStepped, uint8_t(m.rd8(m.ss(), kPlayerStepped) + 1));
        m.wr8(m.ss(), kFloorDamageLatched, 0);
    } else
        update_field_frame_core(m);
    return;
}

// rom: 0x350904
void finish_player_move(Machine &m) {
    finish_player_move_core(m);
    m.retf();
}
// Shared body of ROM 0x350AC3
static void teleport_to_floor(Machine &m, uint8_t floor);
// Dispatch interaction or field-menu input only when stationary and no event is pending.
void process_field_action_core(Machine &m) {
    m.ax() = m.rd16(m.ss(), kMoveDelta);
    if (!m.ax()) {
        m.set_al(m.rd8(m.ss(), kFieldEventPending));
        if (!m.al()) {
            hooks::emit(hooks::Event::FieldIdle, m);
            if (const int32_t floor =
                    hooks::filter(hooks::Value::FieldTeleport, m, 0, {m.rd8(m.ss(), kTowerCounter)});
                floor > 0) [[unlikely]] {
                if (floor <= m.rd8(m.ss(), kTowerCounter) && floor < 24 && m.rd8(m.ss(), kTravelMode) == 0)
                    teleport_to_floor(m, uint8_t(floor));
                return;
            }
            m.ax() = m.rd16(m.ss(), kKeysPressed);
            if (m.ax() & 4)
                interact_with_facing_cell_core(m);
            else if (m.ax() & 0xF02)
                try_open_field_menu_core(m);
        }
    }
    return;
}

// rom: 0x350AC3 (near)
// Dispatch interaction or field-menu input only when stationary and no event is pending.
void process_field_action(Machine &m) {
    process_field_action_core(m);
    m.ret();
}
// The Teleport to Floor cheat (hooks::Value::FieldTeleport): the Door's warp without the menu -- the field
// display kept, the floor's entry set and the warp animated (animate_destination_warp), then the field
// restored with the new map loaded and faded in, as try_open_field_menu does after the menu.
static void teleport_to_floor(Machine &m, uint8_t floor) {
    backup_field_display_core(m);
    m.wr8(m.ss(), kMenuActionResult, floor);
    animate_destination_warp_core(m);
    {
        const uint16_t native_caller_cs = m.cs();
        m.cs() = 0x5000;
        restore_field_after_menu_core(m);
        m.cs() = native_caller_cs;
    }
    fade_field_to_target_core(m);
}

// Shared body of ROM 0x3580FA
// Dispatch a pending field event with script-active set, then clear the interaction record.
void dispatch_field_event_core(Machine &m) {
    const FrameRegisters saved(m);
    m.ax() = 0;
    m.ds() = 0;
    m.ax() = m.rd8(m.ss(), kFieldEventPending);
    if (m.ax()) {
        m.wr8(m.ss(), kFieldEventPending, 0);
        uint8_t active = m.rd8(m.ss(), kScriptActive);
        m.bx() = uint16_t((m.bx() & 0xFF00) | active);
        m.wr8(m.ss(), kScriptActive, 1);
        m.bx() = uint16_t((m.rd8(m.ss(), kFieldEventKind) & 15) * 2);
        m.bx() = m.rd16(m.cs(), uint16_t(m.bx() + 0x8147));
        const uint16_t callback_cs = m.cs();
        field_callback(m, m.bx());
        if (m.cs() != callback_cs)
            return;
        m.set_al(active);
        m.wr8(m.ss(), kScriptActive, active);
        m.ax() = 0;
        m.wr16(m.ss(), kNpcLastMatch, 0);
        m.wr8(m.ss(), kEncounterVariant, 0);
    }
    saved.restore(m);
    return;
}

// rom: 0x3580FA
// Dispatch a pending field event with script-active set, then clear the interaction record.
void dispatch_field_event(Machine &m) {
    dispatch_field_event_core(m);
    if (m.cs() == 0x5000)
        m.retf();
}
// Shared body of ROM 0x350387
// Fade the field toward its target palettes for 16 steps, drawing two field frames per step.
void fade_field_to_target_core(Machine &m) {
    const FrameRegisters saved(m);
    m.wr8(m.ss(), kFieldFadeActive, 1);
    m.ax() = 0xF107;
    frame_out16(m, 0, m.ax());
    for (int step = 0; step < 16; ++step) {
        m.ax() = 0;
        m.cx() = 16;
        palette_step_toward_core(m);
        update_field_frame_core(m);
        update_field_frame_core(m);
    }
    // The final DEC CX (1 -> 0) is live when try_open_field_menu returns its CF result.
    m.flags() =
        uint16_t((m.flags() & ~(emu::PF | emu::AF | emu::ZF | emu::SF | emu::OF)) | emu::PF | emu::ZF);
    m.wr8(m.ss(), kFieldFadeActive, 0);
    saved.restore(m);
    return;
}

// rom: 0x350387
void fade_field_to_target(Machine &m) {
    fade_field_to_target_core(m);
    m.retf();
}
// Shared body of ROM 0x3503BE
// Fade every palette to black over 16 VBlanks, polling input and uploading each step.
void fade_palettes_16_core(Machine &m) {
    hooks::EventScope presentation(m, hooks::Event::PresentationPicture, {3}, {4});
    const FrameRegisters saved(m);
    for (int step = 0; step < 16; ++step) {
        m.ax() = 0;
        m.cx() = 16;
        darken_palette_step_core(m);
        m.call_far(0x4000, 9);
        m.call_far(0x4000, 0x5c);
        dma_palettes_core(m);
    }
    saved.restore(m);
    return;
}

// rom: 0x3503BE
void fade_palettes_16(Machine &m) {
    fade_palettes_16_core(m);
    m.retf();
}

// Shared body of ROM 0x350676
// Face direction BX, reject blocked cells or NPCs, and queue a walk or NPC interaction.
void attempt_player_step_core(Machine &m) {
    m.wr8(m.ss(), kPlayerFacing, uint8_t(m.bx()));
    m.set_al(m.rd8(m.cs(), uint16_t(m.bx() + 0x0977)));
    m.wr8(m.ss(), kPlayerSpriteFacing, m.al());
    m.bx() = uint16_t(m.bx() * 2);
    m.dx() = m.rd16(m.cs(), uint16_t(m.bx() + 0x097B));
    uint16_t cell = m.rd16(m.ss(), kPlayerCell);
    m.bx() = uint16_t(uint8_t(uint8_t(cell) + uint8_t(m.dx())) |
                      (uint16_t(uint8_t(uint8_t(cell >> 8) + uint8_t(m.dx() >> 8))) << 8));
    m.cx() = m.dx();
    if (uint8_t(m.bx()) < 0x40 && uint8_t(m.bx() >> 8) < 0x40) {
        find_npc_at_cell_core(m);
        if (!(m.flags() & emu::ZF)) {
            m.bx() = m.rd16(m.ss(), kNpcLastMatch);
            uint16_t at = uint16_t(m.bx() + 0x12);
            m.wr8(m.ss(), at, uint8_t(m.rd8(m.ss(), at) + 1));
            m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bx() + 0x11)) & 0x40));
            if (m.al()) {
                queue_matched_npc_interaction_core(m);
                update_field_frame_core(m);
            }
        } else {
            {
                const uint16_t caller_cs = m.cs();
                m.cs() = 0x5000;
                map_tile_flags_word_at_core(m);
                m.cs() = caller_cs;
            }
            if (!(m.ah() & 0x40)) {
                bool allowed = (m.ah() & 0xB0) != 0;
                if (!allowed) {
                    uint16_t old_bx = m.bx();
                    m.bx() = m.rd8(m.ss(), kTravelMode);
                    allowed = (m.rd8(m.cs(), uint16_t(m.bx() + 0x06E5)) & m.al()) == 0;
                    m.bx() = old_bx;
                }
                if (allowed)
                    prepare_player_scroll_core(m);
            }
        }
    }
    return;
}

// rom: 0x350676
void attempt_player_step(Machine &m) {
    attempt_player_step_core(m);
    m.retf();
}
// rom: 0x35098B (near)
// Apply cell triggers, encounters, conveyors and 1..3 floor damage, keeping eligible members at one HP or
// more.
void process_player_cell_entry_core(Machine &m) {
    const FrameRegisters saved(m);
    m.ax() = 0;
    m.ds() = 0;
    m.bx() = m.rd16(m.ss(), kPlayerCell);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        map_tile_flags_word_at_core(m);
        m.cs() = caller_cs;
    }
    if (m.ah() & 0x80) {
        if (m.rd8(m.ss(), kPlayerStepped)) {
            m.set_ah(uint8_t(m.ah() & 0x3F));
            m.wr8(m.ss(), kFieldEventKind, m.ah());
            m.wr8(m.ss(), kFieldEventArgument, m.al());
            m.wr8(m.ss(), kFieldEventPending, 1);
        }
    } else if (!(m.ah() & 0x60)) {
        if (m.rd8(m.ss(), kPlayerStepped)) {
            m.bx() = m.rd16(m.ss(), kPlayerCell);
            {
                const uint16_t caller_cs = m.cs();
                m.cs() = 0x5000;
                map_tile_flags_word_at_core(m);
                m.cs() = caller_cs;
            }
            if (!(m.ax() & 0xB000) && (m.ax() & 0xC0)) {
                uint8_t terrain_flags = m.al();
                m.set_al(uint8_t(((terrain_flags << 2) | (terrain_flags >> 6)) & 3));
                m.set_al(uint8_t(m.al() - 1));
                roll_field_encounter_core(m);
            }
        }
        if (!m.rd8(m.ss(), kTravelMode)) {
            m.bx() = m.rd16(m.ss(), kPlayerCell);
            {
                const uint16_t caller_cs = m.cs();
                m.cs() = 0x5000;
                map_tile_flags_word_at_core(m);
                m.cs() = caller_cs;
            }
            if (m.ah() & 4) {
                m.bx() = uint16_t(m.ah() & 3);
                uint16_t direction = m.bx();
                bool moving =
                    (m.rd16(m.cs(), uint16_t(0x0ABB + direction * 2)) & m.rd16(m.ss(), kMoveDelta)) != 0;
                if (!moving) {
                    m.wr8(m.ss(), kPlayerFacing, uint8_t(direction));
                    m.set_al(m.rd8(m.cs(), uint16_t(direction + 0x0977)));
                    m.wr8(m.ss(), kPlayerSpriteFacing, m.al());
                    test_adjacent_npc_cell_core(m);
                    if (m.flags() & emu::ZF) {
                        uint8_t travel = m.rd8(m.ss(), kTravelMode);
                        m.set_al(3);
                        m.wr8(m.ss(), kTravelMode, 3);
                        m.bx() = uint16_t(m.bx() * 2);
                        m.dx() = m.rd16(m.cs(), uint16_t(0x097B + m.bx()));
                        uint16_t cell = m.rd16(m.ss(), kPlayerCell);
                        m.bx() =
                            uint16_t(uint8_t(uint8_t(cell) + uint8_t(m.dx())) |
                                     (uint16_t(uint8_t(uint8_t(cell >> 8) + uint8_t(m.dx() >> 8))) << 8));
                        m.cx() = m.dx();
                        prepare_player_scroll_core(m);
                        m.set_al(travel);
                        m.wr8(m.ss(), kTravelMode, travel);
                    }
                }
            } else if ((m.ah() & 3) && m.rd8(m.ss(), kPlayerStepped) && !m.rd8(m.ss(), kFloorDamageLatched)) {
                hooks::EventScope audio(m, hooks::Event::SoundContext,
                                        {int(hooks::AudioContext::DamageFloor)});
                m.wr8(m.ss(), kFloorDamageLatched, 1);
                // Terrain damage is the low two bits of the high flag byte (1..3); it never kills or affects
                // dead/stoned members.
                const uint16_t damage =
                    uint16_t(hooks::filter(hooks::Value::FieldDamage, m, m.ah() & 3, {0}));
                for (int i = 0; i < 4 && damage; ++i) {
                    uint16_t member = uint16_t(kGameState + i * 0x1F);
                    if (!(m.rd8(m.ss(), member) & 3)) {
                        uint16_t hp = m.rd16(m.ss(), uint16_t(member + 6));
                        m.wr16(m.ss(), uint16_t(member + 6), hp <= damage ? 1 : uint16_t(hp - damage));
                    }
                }
                m.ax() = 0x18;
                {
                    const uint16_t caller_cs = m.cs();
                    m.cs() = 0xd000;
                    play_sound_effect_entry_core(m);
                    m.cs() = caller_cs;
                }
            }
        }
    }
    m.ax() = 0;
    m.wr8(m.ss(), kPlayerStepped, 0);
    saved.restore(m);
    return;
}
void process_player_cell(Machine &m) {
    process_player_cell_entry_core(m);
    m.ret();
}
void load_field_map_core(Machine &m);
// Shared body of ROM 0x3503FE
// Blank and initialize the field display, player/map/NPC data, then draw its first frame.
void initialize_field_display_core(Machine &m) {
    const FrameRegisters saved(m);
    m.call_far(0x4000, 9);
    m.ax() = 0;
    frame_out16(m, 0, 0);
    m.ax() = 8;
    frame_out16(m, 0x10, 8);
    frame_out16(m, 0x12, 8);
    m.set_al(1);
    m.out8(4, 1);
    m.set_al(0x32);
    m.out8(7, 0x32);
    m.set_al(0);
    m.out8(5, 0);
    m.out8(6, 0);
    for (uint16_t screen = 0; screen < 2; ++screen) {
        m.ax() = screen;
        m.bx() = 0;
        m.cx() = 0x2020;
        m.dx() = 0x1FF;
        fill_screen_rect_core(m);
    }
    m.ax() = 0x9000;
    m.ds() = m.ax();
    m.ax() = 0x100;
    m.cx() = 0x100;
    m.si() = 0x0080;
    copy_tiles_core(m);
    m.ax() = 15;
    m.cx() = 1;
    m.si() = 0;
    m.di() = kPaletteTarget;
    copy_palettes_core(m);
    reset_field_state_core(m);
    initialize_player_sprite_core(m);
    load_field_map_core(m);
    initialize_map_npcs_core(m);
    draw_map_viewport_core(m);
    update_field_frame_core(m);
    saved.restore(m);
    return;
}

// rom: 0x3503FE
void initialize_field_display(Machine &m) {
    initialize_field_display_core(m);
    m.retf();
}
// Load map graphics, mirrored metatiles, collision/encounter tables and compressed map planes.
// Map zero builds the blank map; map 1 may redirect to C8h after its progression flag reaches 2.
void load_field_map_core(Machine &m) {
    const FrameRegisters saved(m);
    uint8_t map = m.rd8(m.ss(), kMapId);
    for (auto pair :
         {std::pair{0x58, 0x3A4C}, std::pair{0x66, 0x3A5D}, std::pair{0x7A, 0x3A6E}, std::pair{0x88, 0x3A7F}})
        if (map == pair.first) {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0xC000;
            switch (pair.second) {
            case 0x3A4C:
                show_achievement_vignette_one_core(m);
                break;
            case 0x3A5D:
                show_achievement_vignette_two_core(m);
                break;
            case 0x3A6E:
                show_achievement_vignette_three_core(m);
                break;
            case 0x3A7F:
                show_achievement_vignette_four_core(m);
                break;
            }
            m.cs() = caller_cs;
        }
    for (uint16_t off : {kPlayerStepped, uint16_t(kFieldPaletteFirstFrame),
                         uint16_t(kFieldPaletteSecondFrame), uint16_t(kFieldTileAnimationFrame)})
        m.wr8(m.ss(), off, 0);
    m.wr8(m.ss(), kFieldTileAnimationTimer, 1);
    m.wr8(m.ss(), kFieldPaletteTimer, 16);
    m.wr8(m.ss(), kFieldPaletteSecondTimer, 16);
    for (int screen = 0; screen < 2; ++screen) {
        m.ax() = uint16_t(screen);
        m.bx() = 0;
        m.cx() = 0x2020;
        m.dx() = 0x1FF;
        fill_screen_rect_core(m);
    }
    for (int i = 0; i < 0x400; ++i)
        m.wr16(kMapSegment, uint16_t(kMapMetatiles + i * 2), 0x1FEF);
    map = m.rd8(m.ss(), kMapId);
    if (!map) {
        m.ax() = 2;
        m.cx() = 0x201;
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0xd000;
            sound_channel_reset_entry_core(m);
            m.cs() = caller_cs;
        }
        m.cx() = 32;
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x4000;
            wait_frames_core(m);
            m.cs() = caller_cs;
        }
        m.ax() = 0;
        m.wr8(m.ss(), kMusicRequest, 0);
        for (int screen = 0; screen < 2; ++screen) {
            m.ax() = uint16_t(screen);
            m.bx() = 0;
            m.cx() = 0x2020;
            m.dx() = 0x1FEF;
            fill_screen_rect_core(m);
        }
        m.ds() = 0x2000;
        m.es() = m.ss();
        m.ax() = 15;
        m.di() = kTileWordFlags;
        m.cx() = 32;
        int step = (m.flags() & emu::DF) ? -2 : 2;
        while (m.cx()) {
            m.wr16(m.es(), m.di(), m.ax());
            m.di() = uint16_t(m.di() + step);
            --m.cx();
        }
        m.di() = kTileByteFlags;
        m.cx() = 32;
        m.set_al(0x80);
        step = (m.flags() & emu::DF) ? -1 : 1;
        while (m.cx()) {
            m.wr8(m.es(), m.di(), m.al());
            m.di() = uint16_t(m.di() + step);
            --m.cx();
        }
        saved.restore(m);
        return;
    }
    if (map == 1) {
        m.set_al(0);
        read_packed_progress_core(m);
        if (m.al() >= 2) {
            map = 0xC8;
            m.wr8(m.ss(), kMapId, map);
            m.wr8(m.ss(), kCurrentFloor, 1);
        }
    }
    if (map < 0x18) {
        m.wr8(m.ss(), kCurrentFloor, map);
        if (map >= m.rd8(m.ss(), kTowerCounter))
            m.wr8(m.ss(), kTowerCounter, map);
    }
    if (map == 0x55)
        m.wr8(m.ss(), kCurrentFloor, 13);
    m.wr16(m.ss(), kCamX, uint16_t((uint16_t(m.rd8(m.ss(), kPlayerCell)) - 7) * 16 + 8));
    m.wr16(m.ss(), kCamY, uint16_t((uint16_t(m.rd8(m.ss(), uint16_t(kPlayerCell + 1))) - 4) * 16));
    m.bp() = uint16_t(0xA830 + map * 18);
    uint16_t record = m.bp();
    uint8_t bank = table_at(m, Table::maps, m.cs(), uint16_t(record + 1));
    m.wr8(m.ss(), kFieldMapBank, bank);
    if (bank) {
        for (int i = 0; i < 4; ++i)
            m.wr16(m.ss(), uint16_t(kFieldMapSource + i * 2),
                   table_at16(m, Table::maps, m.cs(), uint16_t(record + 2 + i * 2)));
        m.wr8(m.ss(), kRunningAllowed, table_at(m, Table::maps, m.cs(), uint16_t(record + 17)));
        for (int i = 0; i < 6; ++i) {
            constexpr uint16_t dest[] = {kEncounterClasses,         kEncounterVariants,
                                         (kEncounterClasses + 0x1), (kEncounterVariants + 0x1),
                                         (kEncounterClasses + 0x2), (kEncounterVariants + 0x2)};
            m.wr8(m.ss(), dest[i], table_at(m, Table::maps, m.cs(), uint16_t(record + 10 + i)));
        }
        uint8_t song = table_at(m, Table::maps, m.cs(), uint16_t(record + 16));
        if (song != m.rd8(m.ss(), kMusicRequestPrev)) {
            m.ax() = 2;
            m.cx() = 0x201;
            {
                const uint16_t caller_cs = m.cs();
                m.cs() = 0xd000;
                sound_channel_reset_entry_core(m);
                m.cs() = caller_cs;
            }
            m.cx() = 32;
            {
                const uint16_t caller_cs = m.cs();
                m.cs() = 0x4000;
                wait_frames_core(m);
                m.cs() = caller_cs;
            }
            m.ax() = 0;
            m.wr8(m.ss(), kMusicRequest, 0);
            m.wr8(m.ss(), kMusicRequestPrev, 0);
            {
                const uint16_t caller_cs = m.cs();
                m.cs() = 0xd000;
                sound_request_core(m);
                m.cs() = caller_cs;
            }
            m.ax() = 0;
            {
                const uint16_t caller_cs = m.cs();
                m.cs() = 0xd000;
                play_sound_effect_entry_core(m);
                m.cs() = caller_cs;
            }
            m.call_far(0x4000, 9);
            m.call_far(0x4000, 9);
            m.ax() = table_at(m, Table::maps, m.cs(), uint16_t(record + 16));
            m.wr8(m.ss(), kMusicRequest, m.al());
        }
        uint8_t graphics = table_at(m, Table::maps, m.cs(), record);
        m.wr8(m.ss(), kFieldTilesetIndex, graphics);
        m.out8(0xC2, uint8_t((graphics >> 2) + 0xC0));
        m.ds() = 0x2000;
        m.bp() = uint16_t(0xB718 + graphics * 8);
        uint16_t gfx = m.bp();
        m.wr16(m.ss(), kFieldAnimationTileSource, table_at16(m, Table::tilesets, m.cs(), uint16_t(gfx + 2)));
        m.wr16(m.ss(), kFieldAnimationPaletteSource,
               table_at16(m, Table::tilesets, m.cs(), uint16_t(gfx + 4)));
        m.ax() = 0x200;
        m.cx() = table_at16(m, Table::tilesets, m.cs(), gfx);
        m.si() = table_at16(m, Table::tilesets, m.cs(), uint16_t(gfx + 2));
        copy_tiles_core(m);
        auto copy_words = [&](uint16_t source, uint16_t target, uint16_t count) {
            m.si() = source;
            m.di() = target;
            m.cx() = count;
            int step = (m.flags() & emu::DF) ? -2 : 2;
            while (m.cx()) {
                m.wr16(m.es(), m.di(), m.rd16(m.ds(), m.si()));
                m.di() = uint16_t(m.di() + step);
                m.si() = uint16_t(m.si() + step);
                --m.cx();
            }
        };
        m.out8(0xC2, 0xCF);
        m.ds() = 0x2000;
        m.es() = m.ss();
        copy_words(table_at16(m, Table::tilesets, m.cs(), uint16_t(gfx + 6)), kLzDictionary, 0x100);
        m.ax() = 0;
        m.cx() = 4;
        m.si() = table_at16(m, Table::tilesets, m.cs(), uint16_t(gfx + 4));
        m.di() = kPaletteTarget;
        copy_palettes_core(m);
        m.out8(0xC2, 0xE6);
        m.ds() = 0x2000;
        m.es() = m.ss();
        map = m.rd8(m.ss(), kMapId);
        uint16_t index = uint8_t(map - 1);
        copy_words(uint16_t(index * 64), kTileWordFlags, 32);
        copy_words(uint16_t(0x34C0 + index * 32), kTileByteFlags, 16);
        m.out8(0xC2, m.rd8(m.ss(), kFieldMapBank));
        m.ds() = 0x2000;
        m.es() = 0x1000;
        m.si() = m.rd16(m.ss(), kFieldMetatileSource);
        m.di() = kMapMetatiles;
        constexpr uint16_t destinations[4][4] = {{0, 2, 4, 6}, {2, 0, 6, 4}, {4, 6, 0, 2}, {6, 4, 2, 0}};
        for (int tile = 0; tile < 256; ++tile) {
            uint8_t id = m.rd8(m.ds(), m.si());
            uint8_t mirror =
                uint8_t((m.rd8(m.ds(), uint16_t(m.rd16(m.ss(), kFieldMetatileMirrorSource) + tile / 4))
                         << ((tile & 3) * 2)) &
                        0xC0);
            uint16_t first = uint16_t(id * 4 + 0x2000 + (uint16_t(mirror) << 8));
            for (int quadrant = 0; quadrant < 4; ++quadrant) {
                uint8_t palette = m.rd8(m.ss(), uint16_t(kLzDictionary + id * 4 + quadrant));
                uint16_t word = uint16_t(first + quadrant) | uint16_t(uint16_t(uint8_t(palette * 2)) << 8);
                m.wr16(m.es(), uint16_t(m.di() + destinations[mirror >> 6][quadrant]), word);
            }
            ++m.si();
            m.di() = uint16_t(m.di() + 8);
        }
        m.ds() = 0x2000;
        m.es() = 0x1000;
        m.si() = m.rd16(m.ss(), kFieldMapSource);
        m.di() = 0;
        m.cx() = 0xC80;
        decompress_field_map_core(m);
        m.ds() = 0x2000;
        m.es() = 0x1000;
        m.si() = m.rd16(m.ss(), kFieldAttributeSource);
        m.di() = kMapAttributes;
        m.cx() = 0x800;
        decompress_field_map_core(m);
    }
    saved.restore(m);
}
// rom: 0x351334
// See load_field_map_core; unregistered because pending IRQ state differs in differential tests.
void load_field_map(Machine &m) {
    load_field_map_core(m);
    m.retf();
}

// Shared body of ROM 0x350000
// Clear transient field controls/events, NPCs and the palette buffer before entering the field.
void initialize_field_controls_core(Machine &m) {
    m.ax() = 0;
    for (uint16_t off : {uint16_t(kOamCount), uint16_t(kPlayerFacing), uint16_t(kFieldOverlayMode)})
        m.wr8(m.ss(), off, 0);
    m.set_al(1);
    for (uint16_t off : {uint16_t(kBlinkEnabled), uint16_t(kEncountersEnabled), uint16_t(kFieldControlFlag)})
        m.wr8(m.ss(), off, 1);
    clear_field_event_core(m);
    clear_field_interaction_core(m);
    clear_field_npcs_core(m);
    m.di() = kPaletteBuffer;
    clear_field_word_buffer_core(m);
    return;
}

// rom: 0x350000
void initialize_field_controls(Machine &m) {
    initialize_field_controls_core(m);
    m.retf();
}
// Shared body of ROM 0x3504AF
// Restore field palette/display after a menu; optionally reload the map, then rebuild player/NPCs.
void restore_field_after_menu_core(Machine &m) {
    const FrameRegisters saved(m);
    m.call_far(0x4000, 9);
    m.ax() = 0;
    frame_out16(m, 0, m.ax());
    m.ax() = 8;
    frame_out16(m, 0x10, m.ax());
    frame_out16(m, 0x12, m.ax());
    m.set_al(1);
    m.out8(4, m.al());
    m.set_al(0x32);
    m.out8(7, m.al());
    m.set_al(0);
    m.out8(5, 0);
    m.out8(6, 0);
    m.ax() = 8;
    m.wr16(m.ss(), kFieldScroll, 8);
    m.wr16(m.ss(), kFieldScroll + 2, 8);
    m.ax() = m.ss();
    m.ds() = m.ax();
    m.es() = m.ax();
    m.di() = kSpriteBuffer;
    m.si() = kDisplayBackup;
    m.cx() = 0x400;
    while (m.cx()) {
        m.wr16(m.es(), m.di(), m.rd16(m.ds(), m.si()));
        int step = (m.flags() & emu::DF) ? -2 : 2;
        m.di() = uint16_t(m.di() + step);
        m.si() = uint16_t(m.si() + step);
        --m.cx();
    }
    for (uint16_t screen : {uint16_t(0), uint16_t(1)}) {
        m.ax() = screen;
        m.bx() = 0;
        m.cx() = 0x2020;
        m.dx() = 0x1FF;
        fill_screen_rect_core(m);
    }
    m.ax() = 0x9000;
    m.ds() = m.ax();
    m.ax() = 0x100;
    m.cx() = 0x100;
    m.si() = 0x0080;
    copy_tiles_core(m);
    m.ax() = 15;
    m.cx() = 1;
    m.si() = 0;
    m.di() = kPaletteTarget;
    copy_palettes_core(m);
    m.set_al(m.rd8(m.ss(), kSavedMapChanged));
    if (m.al())
        initialize_field_display_core(m);
    initialize_player_sprite_core(m);
    draw_map_viewport_core(m);
    backup_field_npcs_core(m);
    initialize_map_npcs_core(m);
    restore_field_npcs_core(m);
    saved.restore(m);
    return;
}

// rom: 0x3504AF
// Restore field palette/display after a menu; optionally reload the map, then rebuild player/NPCs.
void restore_field_after_menu(Machine &m) {
    restore_field_after_menu_core(m);
    m.retf();
}
// Shared body of ROM 0x350B64
// Open the field menu if pressed keys select a supported category.
void try_open_field_menu_core(Machine &m) {
    m.ax() = m.rd16(m.ss(), kKeysPressed);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        test_menu_categories_core(m);
        m.cs() = caller_cs;
    }
    if (!(m.flags() & emu::CF)) {
        m.flags() |= emu::CF;
        return;
    }
    uint16_t selection = m.ax();
    fade_palettes_16_core(m);
    backup_field_display_core(m);
    m.ax() = 0;
    logic_flags(m, 0);
    for (uint16_t off :
         {uint16_t(kFieldPreSpriteCallback), uint16_t(kFieldPostSpriteCallback), uint16_t(kFieldNpcCallback)})
        m.wr16(m.ss(), off, 0);
    m.ax() = selection;
    m.bx() = 0;
    logic_flags(m, 0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_party_menu_core(m);
        m.cs() = caller_cs;
    };
    fade_palettes_16_core(m);
    if (auto *session = Session::find(m.wsc()); session && session->party_load_pending) {
        session->party_load_pending = false;
        // Title Continue enters game_main, which initializes controls and the complete loaded map.
        // Restoring the pre-menu field would instead mix the old map/NPCs with the loaded game.
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        initialize_field_controls_core(m);
        reset_field_state_core(m);
        initialize_field_display_core(m);
        fade_field_to_target_core(m);
        m.cs() = caller_cs;
        m.flags() &= ~emu::CF;
        return;
    }
    {
        const uint16_t native_caller_cs = m.cs();
        m.cs() = 0x5000;
        restore_field_after_menu_core(m);
        m.cs() = native_caller_cs;
    }
    fade_field_to_target_core(m);
    m.flags() &= ~emu::CF;
    return;
}

// rom: 0x350B64 (near)
// Open the field menu if pressed keys select a supported category.
void try_open_field_menu(Machine &m) {
    try_open_field_menu_core(m);
    m.ret();
}
// Shared body of ROM 0x3510A4
// Transition from the field into the battle engine and rebuild/fade in the field on return.
bool enter_field_battle_core(Machine &m) {
    const FrameRegisters saved(m);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        clear_field_overlay_core(m);
        m.cs() = caller_cs;
    };
    animate_battle_entry_core(m);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x6000;
        if (!run_battle_core(m))
            return false;
        m.cs() = caller_cs;
    };
    initialize_field_display_core(m);
    fade_field_to_target_core(m);
    saved.restore(m);
    return true;
}

// rom: 0x3510A4
// Transition from the field into the battle engine and rebuild/fade in the field on return.
void enter_field_battle(Machine &m) {
    if (enter_field_battle_core(m))
        m.retf();
}
// Shared body of ROM 0x3510C6
void animate_battle_entry_core(Machine &m) {
    hooks::EventScope audio(m, hooks::Event::SoundContext, {int(hooks::AudioContext::BattleEntry)});
    const FrameRegisters saved(m);
    m.ax() = 5;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        m.cs() = caller_cs;
    }
    m.call_far(0x4000, 9);
    m.ax() = 1;
    m.bx() = 0;
    m.cx() = 0x2020;
    m.dx() = 0x1FEF;
    fill_screen_rect_core(m);
    m.ax() = 0;
    frame_out16(m, 8, 0);
    m.ax() = 0x90E0;
    frame_out16(m, 10, m.ax());
    m.ax() = 0xF137;
    frame_out16(m, 0, m.ax());
    m.ax() = 0;
    m.ds() = 0;
    logic_flags(m, 0);
    for (uint16_t off : {uint16_t(kBattleWipeScroll), uint16_t(kBattleWipeSecondScroll),
                         uint16_t(kBattleWipeThirdScroll), uint16_t(kBattleWipeFourthScroll)})
        m.wr16(m.ss(), off, 0);
    m.set_al(0);
    m.out8(3, 0);
    m.bx() = kLineCompareInterruptVector;
    m.wr16(0, kLineCompareInterruptVector, 0x1310);
    m.wr16(0, (kLineCompareInterruptVector + 2), 0x5000);
    m.set_al(uint8_t(m.in8(0xB2) | 0x10));
    m.out8(0xB2, m.al());
    for (uint16_t frame = 40; frame; --frame) {
        m.cx() = frame;
        if (frame < 16) {
            m.ax() = 0;
            m.cx() = 16;
            darken_palette_step_core(m);
        }
        m.call_far(0x4000, 9);
        dma_palettes_core(m);
        m.set_al(4);
        for (uint16_t off : {uint16_t(kBattleWipeScroll), uint16_t(kBattleWipeSecondScroll)})
            m.wr8(m.ss(), off, uint8_t(m.rd8(m.ss(), off) + 4));
        for (uint16_t off : {uint16_t(kBattleWipeThirdScroll), uint16_t(kBattleWipeFourthScroll)})
            m.wr8(m.ss(), off, uint8_t(m.rd8(m.ss(), off) - 4));
        m.set_al(1);
        for (uint16_t off : {uint16_t(kBattleWipeFirstX), uint16_t(kBattleWipeThirdX)})
            m.wr8(m.ss(), off, uint8_t(m.rd8(m.ss(), off) + 1));
        for (uint16_t off : {uint16_t(kBattleWipeSecondX), uint16_t(kBattleWipeFourthX)})
            m.wr8(m.ss(), off, uint8_t(m.rd8(m.ss(), off) - 1));
        m.bx() = 0x303;
        m.ax() = uint16_t(m.in8(8) | (m.in8(9) << 8));
        m.ax() = uint16_t(m.ax() + m.bx());
        frame_out16(m, 8, m.ax());
        m.ax() = uint16_t(m.in8(10) | (m.in8(11) << 8));
        m.ax() = uint16_t(m.ax() - m.bx());
        frame_out16(m, 10, m.ax());
    }
    m.cx() = 0;
    m.set_al(uint8_t(m.in8(0xB2) & 0xEF));
    m.out8(0xB2, m.al());
    m.set_al(0x32);
    m.out8(7, m.al());
    saved.restore(m);
    return;
}

// rom: 0x3510C6
// Forty-frame battle wipe: move four scanline scroll pairs and window edges while darkening palettes.
void animate_battle_entry(Machine &m) {
    animate_battle_entry_core(m);
    m.retf();
}

// rom: 0x350564
// Initialize the field and repeatedly poll input, move actors, dispatch events and render a field frame.
// This engine loop does not return; its registry entry is owned by routines_system.cpp.
void game_main(Machine &m) {
    if (auto *session = Session::find(m.wsc()))
        session->party_load_pending = false; // also covers loading from the title's party menu
    {
        const uint16_t native_caller_cs = m.cs();
        m.cs() = 0x5000;
        initialize_field_controls_core(m);
        m.cs() = native_caller_cs;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        reset_field_state_core(m);
        m.cs() = caller_cs;
    };
    {
        const uint16_t native_caller_cs = m.cs();
        m.cs() = 0x5000;
        initialize_field_display_core(m);
        m.cs() = native_caller_cs;
    }
    {
        const uint16_t native_caller_cs = m.cs();
        m.cs() = 0x5000;
        fade_field_to_target_core(m);
        m.cs() = native_caller_cs;
    }
    for (;;) {
        m.set_al(1);
        m.wr8(m.ss(), kAnimationStep, 1);
        m.call_far(0x4000, 0x5c);
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x5000;
            npc_ai_trigger_move_core(m);
            m.cs() = caller_cs;
        }
        hooks::emit(hooks::Event::PresentationField, m, {0});
        process_field_direction_entry_core(m);
        process_player_cell_entry_core(m);
        process_field_action_core(m);
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x5000;
            dispatch_field_event_core(m);
            // A battle wipe or ending jumps to main, abandoning this field loop. Preserve the
            // target for the scheduler; restoring CS or drawing another frame loses the restart.
            if (m.cs() != 0x5000)
                return;
            m.cs() = caller_cs;
        };
        hooks::emit(hooks::Event::PresentationField, m, {1});
        finish_player_move_core(m);
    }
}
namespace {

void field_compare8(Machine &m, uint8_t left, uint8_t right) {
    uint8_t difference = uint8_t(left - right);
    uint16_t flags = m.flags() & ~(emu::CF | emu::PF | emu::AF | emu::ZF | emu::SF | emu::OF);
    if (left < right)
        flags |= emu::CF;
    if (!difference)
        flags |= emu::ZF;
    if (difference & 0x80)
        flags |= emu::SF;
    if ((std::popcount(difference) & 1) == 0)
        flags |= emu::PF;
    if ((left ^ right ^ difference) & 0x10)
        flags |= emu::AF;
    if ((left ^ right) & (left ^ difference) & 0x80)
        flags |= emu::OF;
    m.flags() = flags;
}
} // namespace
// Shared body of ROM 0x350328
// Fade the field toward its target with two actor-only updates per palette step, suppressing map effects.
void fade_field_actors_to_target_core(Machine &m) {
    const FrameRegisters saved(m);
    m.wr8(m.ss(), kFieldFadeActive, 1);
    m.ax() = 0xF107;
    frame_out16(m, 0, m.ax());
    m.cx() = 16;
    do {
        uint16_t frame = m.cx();
        m.ax() = 0;
        m.cx() = 16;
        palette_step_toward_core(m);
        for (int repeat = 0; repeat < 2; ++repeat) {
            {
                const uint16_t caller_cs = m.cs();
                m.cs() = 0x5000;
                draw_player_sprite_core(m);
                m.cs() = caller_cs;
            }
            {
                const uint16_t caller_cs = m.cs();
                m.cs() = 0x5000;
                advance_and_draw_npcs_core(m);
                m.cs() = caller_cs;
            }
            {
                const uint16_t caller_cs = m.cs();
                m.cs() = 0x5000;
                advance_player_animation_timers_core(m);
                m.cs() = caller_cs;
            }
            m.call_far(0x4000, 9);
            dma_palettes_core(m);
        }
        m.cx() = frame;
    } while (--m.cx());
    m.wr8(m.ss(), kFieldFadeActive, 0);
    saved.restore(m);
    return;
}

// rom: 0x350328
// Fade the field toward its target with two actor-only updates per palette step, suppressing map effects.
void fade_field_actors_to_target(Machine &m) {
    fade_field_actors_to_target_core(m);
    m.retf();
}
// Shared body of ROM 0x3507A4
// Queue the matched NPC's interaction, unless traveling by boat or the NPC is marked noninteractive.
void queue_matched_npc_interaction_core(Machine &m) {
    m.set_al(m.rd8(m.ss(), kTravelMode));
    if (m.al() == 3) {
        return;
        return;
    }
    m.bx() = m.rd16(m.ss(), kNpcLastMatch);
    if (!m.bx()) {
        return;
        return;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), m.bx()) & 0x80));
    if (m.al()) {
        return;
        return;
    }
    m.set_al(0);
    m.wr8(m.ss(), uint16_t(m.bx() + 0x12), 0);
    m.set_al(m.rd8(m.ss(), uint16_t(m.bx() + 15)));
    if (m.al() >= 16) {
        m.set_al(uint8_t(m.al() - 16));
        m.wr8(m.ss(), kFieldEventPending, m.al());
        m.set_al(m.rd8(m.ss(), uint16_t(m.bx() + 16)));
        m.wr8(m.ss(), kFieldEventArgument, m.al());
        m.set_al(3);
        m.wr8(m.ss(), kFieldEventKind, 3);
    } else {
        m.wr8(m.ss(), kFieldEventKind, m.al());
        m.set_al(m.rd8(m.ss(), uint16_t(m.bx() + 16)));
        m.wr8(m.ss(), kFieldEventArgument, m.al());
        m.set_al(1);
        m.wr8(m.ss(), kFieldEventPending, 1);
        face_interacting_npc_core(m);
        update_field_frame_core(m);
    }
    return;
}

// rom: 0x3507A4 (near)
void queue_matched_npc_interaction(Machine &m) {
    queue_matched_npc_interaction_core(m);
    m.ret();
}
// Shared body of ROM 0x3507FD
// Force one step in the current facing direction, bypassing collision checks; registers preserved.
void force_player_step_core(Machine &m) {
    const FrameRegisters saved(m);
    m.bx() = m.rd8(m.ss(), kPlayerFacing);
    m.set_al(m.rd8(m.cs(), uint16_t(m.bx() + 0x977)));
    m.wr8(m.ss(), kPlayerSpriteFacing, m.al());
    m.bx() = uint16_t(m.bx() * 2);
    m.dx() = m.rd16(m.cs(), uint16_t(m.bx() + 0x97B));
    m.bx() = m.rd16(m.ss(), kPlayerCell);
    m.bx() = uint16_t(uint8_t(uint8_t(m.bx()) + uint8_t(m.dx())) |
                      (uint16_t(uint8_t(uint8_t(m.bx() >> 8) + uint8_t(m.dx() >> 8))) << 8));
    m.cx() = m.dx();
    prepare_player_scroll_core(m);
    finish_player_move_core(m);
    saved.restore(m);
    return;
}

// rom: 0x3507FD
// Force one step in the current facing direction, bypassing collision checks; registers preserved.
void force_player_step(Machine &m) {
    force_player_step_core(m);
    m.retf();
}
// Shared body of ROM 0x350AEE
// Interact with the cell in front: NPC, terrain event, or vehicle exit; preserve registers.
void interact_with_facing_cell_core(Machine &m) {
    const FrameRegisters saved(m);
    m.ax() = m.rd8(m.ss(), kPlayerFacing);
    m.bx() = uint16_t(m.ax() * 2);
    m.ax() = m.rd16(m.cs(), uint16_t(m.bx() + 0xB5C));
    m.bx() = m.rd16(m.ss(), kPlayerCell);
    m.bx() =
        uint16_t(uint8_t(uint8_t(m.bx()) + m.al()) | (uint16_t(uint8_t(uint8_t(m.bx() >> 8) + m.ah())) << 8));
    uint16_t cell = m.bx();
    find_npc_at_cell_core(m);
    m.bx() = cell;
    if (!(m.flags() & emu::ZF))
        queue_matched_npc_interaction_core(m);
    else {
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x5000;
            map_tile_flags_word_at_core(m);
            m.cs() = caller_cs;
        }
        if (!(m.ah() & 0x90) && (m.ah() & 0x60)) {
            m.set_ah(uint8_t(m.ah() & 15));
            m.wr8(m.ss(), kFieldEventKind, m.ah());
            m.wr8(m.ss(), kFieldEventArgument, m.al());
            m.set_al(1);
            m.wr8(m.ss(), kFieldEventPending, 1);
        } else {
            m.set_al(m.rd8(m.ss(), kTravelMode));
            if (m.al()) {
                m.set_al(12);
                m.wr8(m.ss(), kFieldEventKind, 12);
                m.set_al(0);
                m.wr8(m.ss(), kFieldEventArgument, 0);
                m.set_al(1);
                m.wr8(m.ss(), kFieldEventPending, 1);
            }
        }
    }
    saved.restore(m);
    return;
}

// rom: 0x350AEE (near)
void interact_with_facing_cell(Machine &m) {
    interact_with_facing_cell_core(m);
    m.ret();
}
// Shared body of ROM 0x35B900
// Select a tower destination, queuing its first-time vignette/event or updating the visited destination.
void choose_tower_destination_core(Machine &m) {
    const FrameRegisters saved(m);
    m.dx() = m.ax();
    m.set_al(10);
    read_packed_progress_core(m);
    field_compare8(m, m.al(), 5);
    if (m.al() != 5) {
        m.set_al(0);
        read_packed_progress_core(m);
        field_compare8(m, m.al(), 2);
        if (m.al() != 2) {
            m.set_al(0x10);
            m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.dx()));
            m.set_al(uint8_t(0x10 << uint8_t(m.cx() & 31)));
            m.set_ah(uint8_t(m.rd8(m.ss(), kProgressionBits) & m.al()));
            logic_flags(m, uint16_t(m.ah()));
            if (!m.ah()) {
                m.wr8(m.ss(), kProgressionBits, uint8_t(m.rd8(m.ss(), kProgressionBits) | m.al()));
                m.set_al(uint8_t(0x2B + uint8_t(m.dx())));
                m.wr8(m.ss(), kFieldEventArgument, m.al());
                m.set_al(2);
                m.wr8(m.ss(), kFieldEventKind, 2);
                m.wr8(m.ss(), kFieldEventPending, 2);
                m.wr8(m.ds(), kMenuExitRequested, 5);
                m.set_al(1);
                m.wr8(m.ss(), kPlayerFacing, 1);
                m.set_al(9);
                m.wr8(m.ss(), kPlayerSpriteFacing, 9);
                m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(uint8_t(m.dx()) + 5));
                m.set_al(4);
                write_packed_progress_core(m);
            } else {
                m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(uint8_t(m.dx()) + 5));
                m.set_al(4);
                write_packed_progress_core(m);
                m.ax() = 0x66;
                {
                    const uint16_t caller_cs = m.cs();
                    m.cs() = 0xd000;
                    play_sound_effect_entry_core(m);
                    m.cs() = caller_cs;
                }
                m.cx() = 75;
                {
                    const uint16_t caller_cs = m.cs();
                    m.cs() = 0x4000;
                    wait_frames_core(m);
                    m.cs() = caller_cs;
                }
                m.wr8(m.ds(), kMenuExitRequested, 0);
            }
        } else {
            m.set_al(m.rd8(m.ss(), kMapId));
            field_compare8(m, m.al(), 0xC8);
            if (m.al() == 0xC8) {
                m.set_al(0x2F);
                m.wr8(m.ss(), kFieldEventArgument, m.al());
                m.set_al(2);
                m.wr8(m.ss(), kFieldEventKind, 2);
                m.wr8(m.ss(), kFieldEventPending, 2);
                m.wr8(m.ds(), kMenuExitRequested, 5);
                m.set_al(1);
                m.wr8(m.ss(), kPlayerFacing, 1);
                m.set_al(10);
                m.dx() = uint16_t((m.dx() & 0xFF00) | 5);
                write_packed_progress_core(m);
            }
        }
    }
    saved.restore(m);
    m.flags() &= ~emu::CF;
    return;
}

// rom: 0x35B900
void choose_tower_destination(Machine &m) {
    choose_tower_destination_core(m);
    m.retf();
}
// Shared body of ROM 0x35B9B9
// Install the first key at the tower lock, consuming its item and reporting success in CF.
void use_first_tower_key_core(Machine &m) {
    const FrameRegisters saved(m);
    m.dx() = 0x0204;
    m.set_al(0x11);
    install_tower_key_core(m);
    saved.restore(m);
    m.flags() &= ~emu::CF;
    return;
}

// rom: 0x35B9B9
// Install the first key at the tower lock, consuming its item and reporting success in CF.
void use_first_tower_key(Machine &m) {
    use_first_tower_key_core(m);
    m.retf();
}
// Shared body of ROM 0x35B9CC
// Install the second key; preserve the lock helper's CF result.
void use_second_tower_key_core(Machine &m) {
    const FrameRegisters saved(m);
    m.dx() = 0x0303;
    m.set_al(0x12);
    install_tower_key_core(m);
    saved.restore(m);
    return;
}

// rom: 0x35B9CC
// Install the second key; preserve the lock helper's CF result.
void use_second_tower_key(Machine &m) {
    use_second_tower_key_core(m);
    m.retf();
}
// Shared body of ROM 0x35B9DE
// Install the third key, then report success even when the common lock helper rejected it.
void use_third_tower_key_core(Machine &m) {
    const FrameRegisters saved(m);
    m.dx() = 0x0402;
    m.set_al(0x13);
    install_tower_key_core(m);
    saved.restore(m);
    // Original-game quirk kept: failure from the lock helper is discarded, unlike the first two keys.
    m.flags() &= ~emu::CF;
    return;
}

// rom: 0x35B9DE
// Install the third key; original wrapper clears CF even if the lock helper rejects it.
void use_third_tower_key(Machine &m) {
    use_third_tower_key_core(m);
    m.retf();
}
// Shared body of ROM 0x35B9F1
// Consume a held key when facing either lock cell, update packed progression and queue final unlock event.
void install_tower_key_core(Machine &m) {
    const FrameRegisters saved(m);
    find_party_item_core(m);
    bool valid = (m.flags() & emu::ZF) != 0;
    if (valid) {
        m.set_al(m.rd8(m.ss(), kMapId));
        field_compare8(m, m.al(), 0x4F);
        valid = m.al() == 0x4F;
    }
    if (valid) {
        m.bx() = uint16_t(m.rd8(m.ss(), kPlayerFacing) * 2);
        m.ax() = m.rd16(m.ss(), kPlayerCell);
        m.bx() = m.rd16(m.cs(), uint16_t(m.bx() + 0xBA86));
        m.set_al(uint8_t(m.al() + uint8_t(m.bx())));
        m.set_ah(uint8_t(m.ah() + uint8_t(m.bx() >> 8)));
        valid = m.ax() == 0x1813 || m.ax() == 0x1713;
    }
    if (!valid) {
        m.ax() = 0;
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x4000;
            wait_unusable_item_message_core(m);
            m.cs() = caller_cs;
        };
        saved.restore(m);
        m.flags() |= emu::CF;
        return;
    }
    m.set_al(uint8_t(m.dx()));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.dx() >> 8));
    write_packed_progress_core(m);
    m.set_al(0xFF);
    m.wr16(m.ss(), m.si(), 0xFFFF);
    m.set_al(1);
    read_packed_progress_core(m);
    field_compare8(m, m.al(), 3);
    if (m.al() != 3) {
        m.set_al(uint8_t(m.al() + 1));
        uint16_t value = m.ax();
        m.dx() = uint16_t((m.dx() & 0xFF00) | m.al());
        m.set_al(1);
        write_packed_progress_core(m);
        m.ax() = value;
        field_compare8(m, m.al(), 3);
        if (m.al() == 3) {
            m.set_al(10);
            m.dx() = uint16_t((m.dx() & 0xFF00) | 1);
            write_packed_progress_core(m);
            m.set_al(0xE6);
            m.wr8(m.ss(), kFieldEventArgument, m.al());
            m.set_al(9);
            m.wr8(m.ss(), kFieldEventKind, 9);
            m.set_al(1);
            m.wr8(m.ss(), kFieldEventPending, 1);
            m.wr8(m.ds(), kMenuExitRequested, 5);
        }
    }
    saved.restore(m);
    m.flags() &= ~emu::CF;
    return;
}

// rom: 0x35B9F1 (near)
// Consume a held key when facing either lock cell, update packed progression and queue final unlock event.
void install_tower_key(Machine &m) {
    install_tower_key_core(m);
    m.ret();
}
// Shared body of ROM 0x35BA98
// Combine the two progression items, consume both, and queue the associated field event.
void combine_progression_items_core(Machine &m) {
    const FrameRegisters saved(m);
    m.set_al(0x15);
    find_party_item_core(m);
    bool valid = (m.flags() & emu::ZF) != 0;
    if (valid) {
        m.di() = m.si();
        m.set_al(0x16);
        find_party_item_core(m);
        valid = (m.flags() & emu::ZF) != 0;
    }
    if (valid) {
        m.set_al(0xFF);
        m.wr16(m.ss(), m.si(), 0xFFFF);
        m.wr16(m.ss(), m.di(), 0xFFFF);
        m.set_al(10);
        m.dx() = uint16_t((m.dx() & 0xFF00) | 2);
        write_packed_progress_core(m);
        m.set_al(5);
        m.dx() = uint16_t((m.dx() & 0xFF00) | 6);
        write_packed_progress_core(m);
        m.set_al(0);
        m.wr8(m.ss(), kPlayerFacing, 0);
        m.set_al(2);
        m.wr8(m.ss(), kFieldEventKind, 2);
        m.set_al(0x13);
        m.wr8(m.ss(), kFieldEventArgument, m.al());
        m.set_al(1);
        m.wr8(m.ss(), kFieldEventPending, 1);
        m.wr8(m.ds(), kMenuExitRequested, 5);
    } else {
        m.ax() = 0;
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x4000;
            wait_orb_pair_missing_message_core(m);
            m.cs() = caller_cs;
        };
    }
    saved.restore(m);
    return;
}

// rom: 0x35BA98 (near)
// Combine the two progression items, consume both, and queue the associated field event.
void combine_progression_items(Machine &m) {
    combine_progression_items_core(m);
    m.ret();
}
// Shared body of ROM 0x35BA8E
// Try combining the progression items, then report success even when the pair was unavailable.
void use_first_progression_item_core(Machine &m) {
    combine_progression_items_core(m);
    // Original-game quirk kept: an unavailable combination still reports success to the item dispatcher.
    m.flags() &= ~emu::CF;
    return;
}

// rom: 0x35BA8E
// Combine progression items; this wrapper always clears CF.
void use_first_progression_item(Machine &m) {
    use_first_progression_item_core(m);
    m.retf();
}
// Shared body of ROM 0x35BA93
// Use the second entry to the combination helper, also clearing CF on failure.
void use_second_progression_item_core(Machine &m) {
    combine_progression_items_core(m);
    // Original-game quirk kept: the second combination entry also reports success on failure.
    m.flags() &= ~emu::CF;
    return;
}

// rom: 0x35BA93
// Second item's entry point to the same combination helper, also clearing CF.
void use_second_progression_item(Machine &m) {
    use_second_progression_item_core(m);
    m.retf();
}

// Shared body of ROM 0x35BAFD
// Queue the special progression event on map C4; elsewhere display the unusable-item message.
void use_special_progression_item_core(Machine &m) {
    const FrameRegisters saved(m);
    m.set_al(m.rd8(m.ss(), kMapId));
    field_compare8(m, m.al(), 0xC4);
    if (m.al() == 0xC4) {
        m.set_al(0x11);
        m.wr8(m.ss(), kFieldEventArgument, m.al());
        m.set_al(9);
        m.wr8(m.ss(), kFieldEventKind, 9);
        m.set_al(1);
        m.wr8(m.ss(), kFieldEventPending, 1);
        m.set_al(0xC4);
        m.wr8(m.ss(), kMapId, m.al());
        m.set_al(4);
        m.wr8(m.ss(), kPlayerCell, 4);
        m.set_al(8);
        m.wr8(m.ss(), kPlayerY, 8);
        m.set_al(2);
        m.wr8(m.ss(), kPlayerFacing, 2);
        m.wr8(m.ss(), kSavedMapChanged, 1);
        m.wr8(m.ds(), kMenuExitRequested, 7);
    } else {
        m.ax() = 0;
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x4000;
            wait_unusable_item_message_core(m);
            m.cs() = caller_cs;
        };
    }
    saved.restore(m);
    m.flags() &= ~emu::CF;
    return;
}

// rom: 0x35BAFD
// Queue the special progression event on map C4; elsewhere display the unusable-item message.
void use_special_progression_item(Machine &m) {
    use_special_progression_item_core(m);
    m.retf();
}
// Shared body of ROM 0x35BB52
// Load the selected travel destination and animate its scanline distortion/fade. Zero/out-of-range choices do
// nothing.
void animate_destination_warp_core(Machine &m) {
    hooks::EventScope audio(m, hooks::Event::SoundContext, {int(hooks::AudioContext::Warp)});
    const FrameRegisters saved(m);
    m.bx() = m.rd8(m.ss(), kMenuActionResult);
    if (m.bx() && m.bx() < 24) {
        m.bx() = uint16_t((m.bx() - 1) * 4 + 0x8945);
        m.ax() = 0x5000;
        m.ds() = m.ax();
        for (uint16_t i = 0; i < 4; ++i) {
            m.set_al(m.rd8(m.ds(), uint16_t(m.bx() + i)));
            m.wr8(m.ss(), i == 3 ? kPlayerFacing : uint16_t(kMapId + i), m.al());
        }
        m.set_al(0x66);
        m.wr8(m.ss(), kVehicleLocations, m.al());
        m.set_al(0x20);
        m.wr8(m.ss(), (kVehicleLocations + 0x1), m.al());
        m.set_al(0x1F);
        m.wr8(m.ss(), (kVehicleLocations + 0x2), m.al());
        m.ax() = m.ss();
        m.es() = m.ax();
        m.di() = kEffectDistortion;
        m.ax() = 0;
        m.cx() = 0x80;
        logic_flags(m, 0);
        do {
            m.wr16(m.es(), m.di(), 0);
            m.di() = uint16_t(m.di() + ((m.flags() & emu::DF) ? -2 : 2));
        } while (--m.cx());
        m.ax() = 0x15;
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0xd000;
            play_sound_effect_entry_core(m);
            m.cs() = caller_cs;
        }
        // A native palette fade under the distortion: held host pictures shade and ripple with it.
        hooks::EventScope fade(m, hooks::Event::PresentationPicture, {3}, {4});
        hooks::EventScope warp(m, hooks::Event::TravelWarpEffect, {1}, {0});
        m.set_al(0);
        m.out8(3, 0);
        m.ax() = 0;
        m.ds() = 0;
        m.bx() = kLineCompareInterruptVector;
        m.wr16(0, kLineCompareInterruptVector, 0xBC4D);
        m.wr16(0, (kLineCompareInterruptVector + 2), 0x5000);
        m.set_al(uint8_t(m.in8(0xB2) | 0x10));
        m.out8(0xB2, m.al());
        m.cx() = 0;
        do {
            uint16_t frame = m.cx();
            m.ax() = uint16_t(m.cx() >> 2);
            build_field_distortion_core(m);
            m.cx() = uint16_t(frame + 1);
        } while (m.cx() < 64);
        m.cx() = 16;
        do {
            uint16_t frame = m.cx();
            m.ax() = 16;
            build_field_distortion_core(m);
            m.ax() = 0;
            m.cx() = 16;
            darken_palette_step_core(m);
            m.cx() = 4;
            {
                const uint16_t caller_cs = m.cs();
                m.cs() = 0x4000;
                wait_frames_core(m);
                m.cs() = caller_cs;
            }
            dma_palettes_core(m);
            m.cx() = frame;
        } while (--m.cx());
        m.set_al(0x40);
        m.out8(0xB2, m.al());
        m.wr8(m.ss(), kSavedMapChanged, 1);
    }
    saved.restore(m);
    return;
}

// rom: 0x35BB52
void animate_destination_warp(Machine &m) {
    animate_destination_warp_core(m);
    m.retf();
}
// Shared body of ROM 0x35BDEF
// Consume a travel-item charge and warp when on foot. Vehicle use clears the menu action and reports failure.
void use_destination_travel_item_core(Machine &m) {
    uint8_t mode = m.rd8(m.ss(), kTravelMode);
    field_compare8(m, mode, 0);
    if (mode) {
        m.wr8(m.ss(), kMenuActionResult, 0);
        m.wr8(m.ss(), kMenuExitRequested, 0);
        m.ax() = 0;
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x4000;
            wait_unusable_item_message_core(m);
            m.cs() = caller_cs;
        };
        m.flags() |= emu::CF;
        return;
        return;
    }
    m.set_al(m.rd8(m.ss(), kMapId));
    field_compare8(m, m.al(), 0x4F);
    if (m.al() == 0x4F) {
        uint16_t si = m.si();
        m.set_al(1);
        read_packed_progress_core(m);
        m.si() = si;
        field_compare8(m, m.al(), 3);
    }
    if (hooks::should(hooks::VB::SpendUse, m, true, {1, -1, m.rd8(m.ss(), m.si())})) {
        uint16_t uses_address = uint16_t(m.si() + 1);
        uint8_t previous_uses = m.rd8(m.ss(), uses_address), remaining_uses = uint8_t(previous_uses - 1);
        bool carry = (m.flags() & emu::CF) != 0;
        field_compare8(m, previous_uses, 1);
        m.flags() = uint16_t((m.flags() & ~emu::CF) | (carry ? emu::CF : 0));
        m.wr8(m.ss(), uses_address, remaining_uses);
        if (!remaining_uses) {
            uint8_t id = m.rd8(m.ss(), m.si());
            field_compare8(m, id, 0x80);
            if (id < 0x80)
                m.wr16(m.ss(), m.si(), 0xFFFF);
        }
    }
    animate_destination_warp_core(m);
    m.flags() &= ~emu::CF;
    return;
}

// rom: 0x35BDEF
void use_destination_travel_item(Machine &m) {
    use_destination_travel_item_core(m);
    m.retf();
}

namespace {}
// rom: 0x35A686 (near)
// Six-frame palette animation: cycle two six-palette sequences into palette slots two and three.
void animate_map_palette_cycle_core(Machine &m) {

    m.set_al(uint8_t(m.rd8(m.ss(), kFieldPaletteTimer) - 1));
    m.wr8(m.ss(), kFieldPaletteTimer, m.al());
    if (!m.al()) {
        m.set_al(6);
        m.wr8(m.ss(), kFieldPaletteTimer, m.al());
        for (uint16_t frame : {kFieldPaletteFirstFrame, kFieldPaletteSecondFrame}) {
            m.set_al(uint8_t(m.rd8(m.ss(), frame) + 1));
            if (m.al() >= 6)
                m.set_al(0);
            m.wr8(m.ss(), frame, m.al());
        }
        m.set_al(0xCF);
        m.out8(0xC2, m.al());
        m.ax() = 0x2000;
        m.ds() = m.ax();
        m.ax() = m.ss();
        m.es() = m.ax();
        for (unsigned sequence = 0; sequence < 2; ++sequence) {
            m.ax() = 0;
            m.set_al(uint8_t(m.rd8(m.ss(), sequence ? kFieldPaletteSecondFrame : kFieldPaletteFirstFrame) +
                             (sequence ? 10 : 4)));
            m.ax() = uint16_t((m.ax() << 5) + m.rd16(m.ss(), kFieldAnimationPaletteSource));
            m.si() = m.ax();
            m.di() = sequence ? (kPaletteBuffer + 0x60) : (kPaletteBuffer + 0x40);
            m.cx() = 16;
            do {
                m.wr16(m.es(), m.di(), m.rd16(m.ds(), m.si()));
                const int step = (m.flags() & emu::DF) ? -2 : 2;
                m.si() = uint16_t(m.si() + step);
                m.di() = uint16_t(m.di() + step);
            } while (--m.cx());
        }
    }
}
void animate_map_palette_cycle(Machine &m) {
    animate_map_palette_cycle_core(m);
    m.ret();
}

// rom: 0x35957E (near)
// Every 28 frames upload the next of four tile strips to the map's animated background.
void animate_map_tile_strip_core(Machine &m) {

    m.set_al(uint8_t(m.rd8(m.ss(), kFieldTileAnimationTimer) - 1));
    m.wr8(m.ss(), kFieldTileAnimationTimer, m.al());
    if (!m.al()) {
        m.set_al(28);
        m.wr8(m.ss(), kFieldTileAnimationTimer, m.al());
        m.wr8(m.ss(), kFieldTileAnimationFrame, uint8_t(m.rd8(m.ss(), kFieldTileAnimationFrame) + 1));
        m.set_al(0xED);
        m.out8(0xC2, m.al());
        m.ax() = 0x2000;
        m.ds() = m.ax();
        m.ax() = m.ss();
        m.es() = m.ax();
        m.di() = 0x8000;
        m.ax() = uint16_t((m.rd8(m.ss(), kFieldTileAnimationFrame) & 3) << 7);
        m.si() = uint16_t(m.ax() + 0x700);
        m.cx() = 7;
        do {
            m.ax() = m.si();
            frame_out16(m, 0x40, m.ax());
            m.ax() = 2;
            frame_out16(m, 0x42, m.ax());
            m.ax() = m.di();
            frame_out16(m, 0x44, m.ax());
            m.ax() = 0x80;
            frame_out16(m, 0x46, m.ax());
            m.set_al(0x80);
            m.out8(0x48, m.al());
            m.si() = uint16_t(m.si() + 0x200);
            m.di() = uint16_t(m.di() + 0x80);
        } while (--m.cx());
    }
}
void animate_map_tile_strip(Machine &m) {
    animate_map_tile_strip_core(m);
    m.ret();
}

// rom: 0x358391 (near)
// Event callback: save the current destination, select a map/position from the area's exit table,
// rebuild the display and fade back in. Preserve all registers.
void travel_area_exit_core(Machine &m) {
    const FrameRegisters saved(m);
    clear_field_overlay_core(m);
    fade_palettes_16_core(m);
    m.bx() = uint16_t(m.rd8(m.ss(), kMapId) * 2);
    m.bx() = m.rd16(m.cs(), uint16_t(m.bx() + 0x910D));
    m.ax() = m.rd8(m.ss(), kFieldEventArgument);
    m.bx() = uint16_t((m.bx() + m.ax()) * 4);
    for (unsigned n = 0; n < 3; ++n) {
        m.set_al(m.rd8(m.ss(), uint16_t(kMapId + n)));
        m.wr8(m.ss(), uint16_t(kPreviousMap + n), m.al());
    }
    for (unsigned n = 0; n < 4; ++n) {
        m.set_al(m.rd8(m.cs(), uint16_t(0x8945 + m.bx() + n)));
        m.wr8(m.ss(), n == 3 ? kPlayerFacing : uint16_t(kMapId + n), m.al());
    }
    initialize_field_display_core(m);
    fade_field_to_target_core(m);
    saved.restore(m);
}
void travel_area_exit(Machine &m) {
    travel_area_exit_core(m);
    m.ret();
}

} // namespace sawgod::game

namespace sawgod::game {
namespace {

[[maybe_unused]] constexpr uint16_t kVignetteBackdropColor = 0xFFE0;
} // namespace
// rom: 0x3500AB
// Clear the dialogue layer, set its cursor and install its raster interrupt.
void initialize_field_dialogue_overlay_core(Machine &m) {
    hooks::emit(hooks::Event::Dialogue, m, {1});
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr16(m.ss(), uint16_t(kMoveDelta), uint16_t(m.ax()));
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(kAnimationStep), uint8_t(m.al()));
    m.ax() = uint16_t(0x2);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x0));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x0)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x20));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x20)) << 8));
    m.dx() = uint16_t(0x1FF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        fill_screen_rect_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x2);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x0));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xD)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1C));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x5)) << 8));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        draw_window_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(kOverlayEnabled), uint8_t(m.al()));
    m.ax() = uint16_t(0x386);
    m.wr16(m.ss(), uint16_t(kDialogueCursorPosition), uint16_t(m.ax()));
    m.set_al(uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kDialogueSkipDelay), uint8_t(m.al()));
    m.set_al(uint8_t(0x66));
    m.out8(uint8_t(0x3), m.al());
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.bx() = uint16_t(kLineCompareInterruptVector);
    m.wr16(m.ds(), uint16_t(m.bx()), uint16_t(0x122));
    m.wr16(m.ds(), uint16_t(m.bx() + 0x0002), uint16_t(0x5000));
    m.set_al(uint8_t(m.in8(uint8_t(0xB2))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | 0x10))));
    m.out8(uint8_t(0xB2), m.al());
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void initialize_field_dialogue_overlay(Machine &m) {
    initialize_field_dialogue_overlay_core(m);
    if (m.cs() == 0x5000)
        m.retf();
}
// rom: 0x35059D
// Update the field for CX frames.
void wait_field_frames_core(Machine &m) {
branch_059d:;
    update_field_frame_core(m);
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_059d;
    return;
}
void wait_field_frames(Machine &m) {
    wait_field_frames_core(m);
    if (m.cs() == 0x5000)
        m.retf();
}
// rom: 0x350729 (near)
// Test whether the cell ahead is within bounds, unoccupied and passable.
void test_player_forward_cell_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() ^ m.bx())));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kPlayerFacing))));
    m.bx() =
        uint16_t((m.bx() & 0xFF00) | uint8_t(computed::add<uint8_t>(m, uint8_t(m.bx()), uint8_t(m.bx()))));
    m.bx() = uint16_t(m.rd16(m.cs(), uint16_t(m.bx() + 0x079C)));
    m.bx() =
        uint16_t((m.bx() & 0xFF00) |
                 uint8_t(computed::add<uint8_t>(m, uint8_t(m.bx()), m.rd8(m.ss(), uint16_t(kPlayerCell)))));
    m.bx() = uint16_t(
        (m.bx() & 0x00FF) |
        (uint16_t(uint8_t(computed::add<uint8_t>(m, uint8_t(m.bx() >> 8), m.rd8(m.ss(), uint16_t(kPlayerY)))))
         << 8));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kMapId))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x66);
    if (!(m.flags() & emu::ZF))
        goto branch_0757;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xC);
    if (!(m.flags() & emu::ZF))
        goto branch_0757;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx() >> 8), 0xF);
    if (m.flags() & emu::ZF)
        goto branch_0795;
branch_0757:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kMapId))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6C);
    if (!(m.flags() & emu::ZF))
        goto branch_0769;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0x12);
    if (!(m.flags() & emu::ZF))
        goto branch_0769;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx() >> 8), 0x11);
    if (m.flags() & emu::ZF)
        goto branch_0795;
branch_0769:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0x40);
    if (!(m.flags() & emu::CF))
        goto branch_0795;
    m.bx() = uint16_t(
        (m.bx() & 0x00FF) |
        (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx() >> 8) | uint8_t(m.bx() >> 8)))))
         << 8));
    if (m.flags() & emu::ZF)
        goto branch_0795;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx() >> 8), 0x40);
    if (!(m.flags() & emu::CF))
        goto branch_0795;
    find_npc_at_cell_call_core(m);
    if (!(m.flags() & emu::ZF))
        goto branch_0795;
    map_tile_flags_word_at_core(m);
    (void)computed::logic<uint8_t>(m, uint8_t(m.ah() & 0x40));
    if (!(m.flags() & emu::ZF))
        goto branch_0795;
    (void)computed::logic<uint8_t>(m, uint8_t(m.ah() & 0xB0));
    if (!(m.flags() & emu::ZF))
        goto branch_0791;
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x1))));
    if (!(m.flags() & emu::ZF))
        goto branch_0795;
branch_0791:;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    goto branch_0797;
branch_0795:;
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | 0xFF))));
branch_0797:;
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void test_player_forward_cell(Machine &m) {
    test_player_forward_cell_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x350830 (near)
// Apply facing-dependent movement and scroll the player to the next cell.
void move_player_forward_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() ^ m.bx())));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kPlayerFacing))));
    m.set_al(uint8_t(m.rd8(m.cs(), uint16_t(m.bx() + 0x0977))));
    m.wr8(m.ss(), uint16_t(kPlayerSpriteFacing), uint8_t(m.al()));
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), m.bx()));
    m.dx() = uint16_t(m.rd16(m.cs(), uint16_t(m.bx() + 0x0983)));
    m.bx() = uint16_t(m.rd16(m.ss(), uint16_t(kPlayerCell)));
    m.bx() =
        uint16_t((m.bx() & 0xFF00) | uint8_t(computed::add<uint8_t>(m, uint8_t(m.bx()), uint8_t(m.dx()))));
    m.bx() = uint16_t(
        (m.bx() & 0x00FF) |
        (uint16_t(uint8_t(computed::add<uint8_t>(m, uint8_t(m.bx() >> 8), uint8_t(m.dx() >> 8)))) << 8));
    m.cx() = uint16_t(m.dx());
    prepare_player_scroll_call_core(m);
    finish_player_move_core(m);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void move_player_forward(Machine &m) {
    move_player_forward_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x351197
// Fade palettes and move four raster bands apart while expanding the display window.
void animate_field_split_wipe_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.ax() = uint16_t(uint16_t(m.in8(uint8_t(0x0)) | (uint16_t(m.in8(uint8_t(0x0 + 1))) << 8)));
    computed::push(m, m.ax());
    m.call_far(0x4000, 0x0009);
    m.ax() = uint16_t(0x0);
    computed::out_word(m, uint8_t(0x8), m.ax());
    m.ax() = uint16_t(0x90E0);
    computed::out_word(m, uint8_t(0xA), m.ax());
    m.ax() = uint16_t(0xF137);
    computed::out_word(m, uint8_t(0x0), m.ax());
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(0x0);
    m.cx() = uint16_t(0x2020);
    m.dx() = uint16_t(0x1FEF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        fill_screen_rect_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr16(m.ss(), uint16_t(kBattleWipeScroll), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(kBattleWipeSecondScroll), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(kBattleWipeThirdScroll), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(kBattleWipeFourthScroll), uint16_t(m.ax()));
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x3), m.al());
    computed::push(m, m.ds());
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.bx() = uint16_t(kLineCompareInterruptVector);
    m.wr16(m.ds(), uint16_t(m.bx()), uint16_t(0x1310));
    m.wr16(m.ds(), uint16_t(m.bx() + 0x0002), uint16_t(0x5000));
    m.set_al(uint8_t(m.in8(uint8_t(0xB2))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | 0x10))));
    m.out8(uint8_t(0xB2), m.al());
    m.ds() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(0x28);
branch_11fa:;
    computed::push(m, m.cx());
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x10);
    if (!(m.flags() & emu::CF))
        goto branch_1216;
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x8);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        darken_palette_step_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x9);
    m.cx() = uint16_t(0x6);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        darken_palette_step_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
branch_1216:;
    m.call_far(0x4000, 0x0009);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        dma_palettes_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0x4));
    m.wr8(m.ss(), uint16_t(kBattleWipeScroll),
          uint8_t(computed::add<uint8_t>(m, m.rd8(m.ss(), uint16_t(kBattleWipeScroll)), m.al())));
    m.wr8(m.ss(), uint16_t(kBattleWipeSecondScroll),
          uint8_t(computed::add<uint8_t>(m, m.rd8(m.ss(), uint16_t(kBattleWipeSecondScroll)), m.al())));
    m.wr8(m.ss(), uint16_t(kBattleWipeThirdScroll),
          uint8_t(computed::subtract<uint8_t>(m, m.rd8(m.ss(), uint16_t(kBattleWipeThirdScroll)), m.al())));
    m.wr8(m.ss(), uint16_t(kBattleWipeFourthScroll),
          uint8_t(computed::subtract<uint8_t>(m, m.rd8(m.ss(), uint16_t(kBattleWipeFourthScroll)), m.al())));
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(kBattleWipeFirstX),
          uint8_t(computed::add<uint8_t>(m, m.rd8(m.ss(), uint16_t(kBattleWipeFirstX)), m.al())));
    m.wr8(m.ss(), uint16_t(kBattleWipeSecondX),
          uint8_t(computed::subtract<uint8_t>(m, m.rd8(m.ss(), uint16_t(kBattleWipeSecondX)), m.al())));
    m.wr8(m.ss(), uint16_t(kBattleWipeThirdX),
          uint8_t(computed::add<uint8_t>(m, m.rd8(m.ss(), uint16_t(kBattleWipeThirdX)), m.al())));
    m.wr8(m.ss(), uint16_t(kBattleWipeFourthX),
          uint8_t(computed::subtract<uint8_t>(m, m.rd8(m.ss(), uint16_t(kBattleWipeFourthX)), m.al())));
    m.bx() = uint16_t(0x303);
    m.ax() = uint16_t(uint16_t(m.in8(uint8_t(0x8)) | (uint16_t(m.in8(uint8_t(0x8 + 1))) << 8)));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.bx()));
    computed::out_word(m, uint8_t(0x8), m.ax());
    m.ax() = uint16_t(uint16_t(m.in8(uint8_t(0xA)) | (uint16_t(m.in8(uint8_t(0xA + 1))) << 8)));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.bx()));
    computed::out_word(m, uint8_t(0xA), m.ax());
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_11fa;
    m.set_al(uint8_t(m.in8(uint8_t(0xB2))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0xEF))));
    m.out8(uint8_t(0xB2), m.al());
    m.set_al(uint8_t(0x32));
    m.out8(uint8_t(0x7), m.al());
    m.ax() = uint16_t(computed::pop(m));
    computed::out_word(m, uint8_t(0x0), m.ax());
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void animate_field_split_wipe(Machine &m) {
    animate_field_split_wipe_core(m);
    if (m.cs() == 0x5000)
        m.retf();
}
// rom: 0x351271
// Fade palettes and move four raster bands apart over 24 frames.
void animate_field_split_fade_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr16(m.ss(), uint16_t(kBattleWipeScroll), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(kBattleWipeSecondScroll), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(kBattleWipeThirdScroll), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(kBattleWipeFourthScroll), uint16_t(m.ax()));
    m.set_al(uint8_t(0x0));
    m.out8(uint8_t(0x3), m.al());
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.bx() = uint16_t(kLineCompareInterruptVector);
    m.wr16(m.ds(), uint16_t(m.bx()), uint16_t(0x1310));
    m.wr16(m.ds(), uint16_t(m.bx() + 0x0002), uint16_t(0x5000));
    m.set_al(uint8_t(m.in8(uint8_t(0xB2))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | 0x10))));
    m.out8(uint8_t(0xB2), m.al());
    m.cx() = uint16_t(0x18);
branch_12a5:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0x8);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        darken_palette_step_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x9);
    m.cx() = uint16_t(0x6);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        darken_palette_step_core(m);
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
    m.set_al(uint8_t(0x4));
    m.wr8(m.ss(), uint16_t(kBattleWipeScroll),
          uint8_t(computed::add<uint8_t>(m, m.rd8(m.ss(), uint16_t(kBattleWipeScroll)), m.al())));
    m.wr8(m.ss(), uint16_t(kBattleWipeSecondScroll),
          uint8_t(computed::add<uint8_t>(m, m.rd8(m.ss(), uint16_t(kBattleWipeSecondScroll)), m.al())));
    m.wr8(m.ss(), uint16_t(kBattleWipeThirdScroll),
          uint8_t(computed::subtract<uint8_t>(m, m.rd8(m.ss(), uint16_t(kBattleWipeThirdScroll)), m.al())));
    m.wr8(m.ss(), uint16_t(kBattleWipeFourthScroll),
          uint8_t(computed::subtract<uint8_t>(m, m.rd8(m.ss(), uint16_t(kBattleWipeFourthScroll)), m.al())));
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(kBattleWipeFirstX),
          uint8_t(computed::add<uint8_t>(m, m.rd8(m.ss(), uint16_t(kBattleWipeFirstX)), m.al())));
    m.wr8(m.ss(), uint16_t(kBattleWipeSecondX),
          uint8_t(computed::subtract<uint8_t>(m, m.rd8(m.ss(), uint16_t(kBattleWipeSecondX)), m.al())));
    m.wr8(m.ss(), uint16_t(kBattleWipeThirdX),
          uint8_t(computed::add<uint8_t>(m, m.rd8(m.ss(), uint16_t(kBattleWipeThirdX)), m.al())));
    m.wr8(m.ss(), uint16_t(kBattleWipeFourthX),
          uint8_t(computed::subtract<uint8_t>(m, m.rd8(m.ss(), uint16_t(kBattleWipeFourthX)), m.al())));
    m.bx() = uint16_t(0x303);
    m.ax() = uint16_t(uint16_t(m.in8(uint8_t(0x8)) | (uint16_t(m.in8(uint8_t(0x8 + 1))) << 8)));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.bx()));
    computed::out_word(m, uint8_t(0x8), m.ax());
    m.ax() = uint16_t(uint16_t(m.in8(uint8_t(0xA)) | (uint16_t(m.in8(uint8_t(0xA + 1))) << 8)));
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), m.bx()));
    computed::out_word(m, uint8_t(0xA), m.ax());
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_12a5;
    m.set_al(uint8_t(m.in8(uint8_t(0xB2))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0xEF))));
    m.out8(uint8_t(0xB2), m.al());
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void animate_field_split_fade(Machine &m) {
    animate_field_split_fade_core(m);
    if (m.cs() == 0x5000)
        m.retf();
}
// rom: 0x35282F
// Update field frames until no active NPC has a pending movement.
void wait_npc_movements_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
branch_2833:;
    m.si() = uint16_t(kNpcTable);
    m.cx() = uint16_t(0x10);
branch_2839:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_2848;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x000E))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | m.al()))));
    if (!(m.flags() & emu::ZF))
        goto branch_2853;
branch_2848:;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x14));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_2839;
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
branch_2853:;
    update_field_frame_core(m);
    goto branch_2833;
}
void wait_npc_movements(Machine &m) {
    wait_npc_movements_core(m);
    if (m.cs() == 0x5000)
        m.retf();
}
// rom: 0x35453F (near)
// Initialize the dialogue overlay, process AL's dialogue and wait for its final acknowledgement.
void show_field_dialogue_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    computed::push(m, m.ax());
    {
        const uint16_t callback_cs = m.cs();
        initialize_field_dialogue_overlay_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(computed::pop(m));
    {
        const uint16_t callback_cs = m.cs();
        interpret_field_dialogue_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kOverlayEnabled))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | m.al()))));
    if (m.flags() & emu::ZF)
        goto branch_455a;
    {
        const uint16_t callback_cs = m.cs();
        wait_field_dialogue_acknowledgement_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_455a:;
    clear_field_overlay_core(m);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void show_field_dialogue(Machine &m) {
    show_field_dialogue_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x354568 (near)
// Process a dialogue stream through its end marker.
void interpret_field_dialogue_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    computed::push(m, m.ss());
    computed::push(m, m.ss());
    m.ds() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    {
        const uint16_t callback_cs = m.cs();
        push_indexed_dialogue_frame_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        read_field_dialogue_token_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    (void)computed::subtract<uint8_t>(m, m.ah(), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_4597;
    (void)computed::subtract<uint8_t>(m, m.ah(), 0xC);
    if (m.flags() & emu::ZF)
        goto branch_458a;
    m.bx() = uint16_t(0x386);
    m.wr16(m.ss(), uint16_t(kDialogueCursorPosition), uint16_t(m.bx()));
    goto branch_4592;
branch_458a:;
    {
        const uint16_t callback_cs = m.cs();
        read_field_dialogue_token_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    (void)computed::subtract<uint8_t>(m, m.ah(), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_4597;
branch_4592:;
    {
        const uint16_t callback_cs = m.cs();
        draw_field_dialogue_token_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    goto branch_458a;
branch_4597:;
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void interpret_field_dialogue(Machine &m) {
    interpret_field_dialogue_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x35459C (near)
// Handle a glyph, line break or clear token with dialogue delay and key skipping.
void draw_field_dialogue_token_core(Machine &m) {
    if (m.ah() != 0)
        hooks::emit(hooks::Event::Dialogue, m, {2, m.ah(), 0});
    (void)computed::subtract<uint8_t>(m, m.ah(), 0xA);
    if (!(m.flags() & emu::ZF))
        goto branch_45a4;
    goto branch_463a;
branch_45a4:;
    (void)computed::subtract<uint8_t>(m, m.ah(), 0xD);
    if (!(m.flags() & emu::ZF))
        goto branch_45ac;
    goto branch_463a;
branch_45ac:;
    (void)computed::subtract<uint8_t>(m, m.ah(), 0xE);
    if (m.flags() & emu::ZF)
        goto branch_4611;
    m.dx() = uint16_t(m.ax());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kDialogueSkipDelay))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | m.al()))));
    if (!(m.flags() & emu::ZF))
        goto branch_45e8;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kDialogueSpeed))));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()))));
branch_45c2:;
    update_field_frame_core(m);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        m.call_far(0x4000, 0x005C);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kKeysPressed)));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x4)));
    if (!(m.flags() & emu::ZF))
        goto branch_45e8;
    update_field_frame_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kDialogueSpeed))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xA);
    if (m.flags() & emu::ZF)
        goto branch_45e8;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    if (!(m.flags() & emu::ZF))
        goto branch_45c2;
    goto branch_45ee;
branch_45e8:;
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(kDialogueSkipDelay), uint8_t(m.al()));
branch_45ee:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kOverlayEnabled))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | m.al()))));
    if (m.flags() & emu::ZF)
        goto branch_4610;
    if (Session *session = Session::find(m.wsc()))
        hooks::emit(hooks::Event::Dialogue, m, {2, 0, int32_t(session->dialogue_character(uint8_t(m.dx())))});
    if (Session *session = Session::find(m.wsc()); session && session->dialogue_glyph(m, uint8_t(m.dx()))) {
        // a loaded language's text, typed in our font (text.h); flags/registers as the write below leaves
        // them
        m.bx() = m.rd16(m.ss(), uint16_t(kDialogueCursorPosition));
        m.dx() = uint16_t((m.dx() & 0xFF) | 0x1F00);
        return;
    }
    m.bx() = uint16_t(m.rd16(m.ss(), uint16_t(kDialogueCursorPosition)));
    m.dx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.dx() & 0xFF)));
    m.dx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.dx() | 0x1F00)));
    m.wr16(m.ss(), uint16_t(m.bx() + kOverlayScreen), uint16_t(m.dx()));
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), 0x2));
    m.wr16(m.ss(), uint16_t(kDialogueCursorPosition), uint16_t(m.bx()));
branch_4610:;
    return;
branch_4611:;
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.ax() = uint16_t(0x2);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x1));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xE)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1A));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x3)) << 8));
    m.dx() = uint16_t(0x1FD9);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        fill_screen_rect_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x386);
    m.wr16(m.ss(), uint16_t(kDialogueCursorPosition), uint16_t(m.ax()));
    m.set_al(uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kDialogueSkipDelay), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
branch_463a:;
    m.bx() = uint16_t(m.rd16(m.ss(), uint16_t(kDialogueCursorPosition)));
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), 0x40));
    m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() & 0x7C0)));
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), 0x6));
    (void)computed::subtract<uint16_t>(m, m.bx(), 0x440);
    if (!(m.flags() & emu::CF))
        goto branch_4655;
    m.wr16(m.ss(), uint16_t(kDialogueCursorPosition), uint16_t(m.bx()));
    return;
branch_4655:;
    computed::push(m, m.cx());
    computed::push(m, m.si());
    computed::push(m, m.di());
    computed::push(m, m.ds());
    computed::push(m, m.es());
    m.ax() = uint16_t(m.ss());
    m.ds() = uint16_t(m.ax());
    m.es() = uint16_t(m.ax());
    m.flags() &= ~emu::DF;
    m.si() = uint16_t(0x23C0);
    m.di() = uint16_t(0x2380);
    m.cx() = uint16_t(0x40);
    computed::copy_words(m);
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x2));
    m.ax() = uint16_t(0x1FD9);
    m.cx() = uint16_t(0x1A);
    computed::fill_words(m);
    m.bx() = uint16_t(0x402);
    m.wr16(m.ss(), uint16_t(kDialogueCursorPosition), uint16_t(m.bx()));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    m.di() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    return;
}
void draw_field_dialogue_token(Machine &m) {
    draw_field_dialogue_token_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x354685 (near)
// Decode the next dialogue token, including embedded commands and member-name expansion.
void read_field_dialogue_token_core(Machine &m) {
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.bp());
    computed::push(m, m.si());
    computed::push(m, m.di());
    computed::push(m, m.dx());
    computed::push(m, m.cx());
    computed::push(m, m.bx());
    {
        const uint16_t callback_cs = m.cs();
        current_dialogue_frame_pointer_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0004))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (!(m.flags() & emu::ZF))
        goto branch_469b;
    goto branch_4796;
branch_469b:;
    {
        const uint16_t callback_cs = m.cs();
        read_dialogue_frame_byte_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    (void)computed::subtract<uint8_t>(m, m.al(), 0x20);
    if (m.flags() & emu::CF)
        goto branch_46a5;
    goto branch_4729;
branch_46a5:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x0);
    if (!(m.flags() & emu::ZF))
        goto branch_46ac;
    goto branch_49a8;
branch_46ac:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (!(m.flags() & emu::ZF))
        goto branch_46b3;
    goto branch_49a8;
branch_46b3:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x13);
    if (!(m.flags() & emu::ZF))
        goto branch_46ba;
    goto branch_49a0;
branch_46ba:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (!(m.flags() & emu::CF))
        goto branch_46e0;
    computed::push(m, m.ax());
    {
        const uint16_t callback_cs = m.cs();
        read_dialogue_frame_byte_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.wr8(m.ss(), uint16_t(kFieldEventKind), uint8_t(m.al()));
    {
        const uint16_t callback_cs = m.cs();
        read_dialogue_frame_byte_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.wr8(m.ss(), uint16_t(kFieldEventArgument), uint8_t(m.al()));
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(kFieldEventPending), uint8_t(m.al()));
    {
        const uint16_t callback_cs = m.cs();
        dispatch_field_event_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(computed::pop(m));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1);
    if (m.flags() & emu::ZF)
        goto branch_469b;
    goto branch_49a8;
branch_46e0:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xA);
    if (m.flags() & emu::ZF)
        goto branch_472f;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xC);
    if (m.flags() & emu::ZF)
        goto branch_472f;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xD);
    if (m.flags() & emu::ZF)
        goto branch_472f;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xE);
    if (m.flags() & emu::ZF)
        goto branch_472f;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x11);
    if (!(m.flags() & emu::ZF))
        goto branch_46f7;
    goto branch_4812;
branch_46f7:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x12);
    if (m.flags() & emu::ZF)
        goto branch_4735;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x14);
    if (!(m.flags() & emu::ZF))
        goto branch_4702;
    goto branch_47e0;
branch_4702:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x15);
    if (!(m.flags() & emu::ZF))
        goto branch_4709;
    goto branch_47e8;
branch_4709:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x16);
    if (!(m.flags() & emu::ZF))
        goto branch_4710;
    goto branch_47f0;
branch_4710:;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x19);
    if (m.flags() & emu::ZF)
        goto branch_4750;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1A);
    if (!(m.flags() & emu::ZF))
        goto branch_471b;
    goto branch_4805;
branch_471b:;
    goto branch_4726;
branch_471d:;
    m.bx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.dx() = uint16_t(computed::pop(m));
    m.di() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::pop(m));
    m.bp() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    return;
branch_4726:;
    goto branch_49a8;
branch_4729:;
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x20)));
    m.set_ah(uint8_t(0x0));
    goto branch_471d;
branch_472f:;
    m.set_ah(uint8_t(m.al()));
    m.set_al(uint8_t(0x0));
    goto branch_471d;
branch_4735:;
    {
        const uint16_t callback_cs = m.cs();
        read_dialogue_frame_byte_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.dx() = uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    {
        const uint16_t callback_cs = m.cs();
        read_dialogue_frame_byte_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.al()));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.dx()));
    computed::push(m, m.bx());
    m.bx() = uint16_t(m.dx());
    m.dx() = uint16_t(m.rd16(m.cs(), uint16_t(m.bx() + 0x7B62)));
    m.bx() = uint16_t(computed::pop(m));
    {
        const uint16_t callback_cs = m.cs();
        set_dialogue_frame_cursor_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    goto branch_469b;
branch_4750:;
    {
        const uint16_t callback_cs = m.cs();
        current_dialogue_frame_pointer_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(m.si() + 0x0004), uint8_t(m.al()));
    m.set_al(uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(m.si() + 0x0006), uint8_t(m.al()));
    {
        const uint16_t callback_cs = m.cs();
        read_dialogue_frame_byte_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_476a;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kDialogueLeader))));
branch_476a:;
    m.wr8(m.ss(), uint16_t(m.si() + 0x0005), uint8_t(m.al()));
branch_476e:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0005))));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1F));
    {
        const uint16_t product = uint16_t(m.al()) * uint8_t(m.cx());
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), kGameState));
    m.bx() = uint16_t(m.ax());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bx()))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_4796;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0005))));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x1)));
    m.wr8(m.ss(), uint16_t(m.si() + 0x0005), uint8_t(m.al()));
    if (!(m.flags() & emu::CF))
        goto branch_476e;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kDialogueLeader))));
    m.wr8(m.ss(), uint16_t(m.si() + 0x0005), uint8_t(m.al()));
branch_4796:;
    {
        const uint16_t callback_cs = m.cs();
        current_dialogue_frame_pointer_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    hooks::emit(hooks::Event::Dialogue, m,
                {4, m.rd8(m.ss(), uint16_t(m.si() + 5)), m.rd8(m.ss(), uint16_t(m.si() + 6))});
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0005))));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1F));
    {
        const uint16_t product = uint16_t(m.al()) * uint8_t(m.cx());
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), kMemberName));
    m.bx() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0006))));
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), m.ax()));
    computed::push(m, m.bx());
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(m.si() + 0x0006), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::ZF)
        goto branch_47cb;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.bx() = uint16_t(computed::increment<uint16_t>(m, m.bx()));
branch_47bc:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bx()))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xD9);
    if (!(m.flags() & emu::ZF))
        goto branch_47d1;
    m.bx() = uint16_t(computed::increment<uint16_t>(m, m.bx()));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()))));
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x4);
    if (!(m.flags() & emu::ZF))
        goto branch_47bc;
branch_47cb:;
    m.set_al(uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(m.si() + 0x0004), uint8_t(m.al()));
branch_47d1:;
    m.bx() = uint16_t(computed::pop(m));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bx()))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_47db;
    m.set_al(uint8_t(0xD9));
branch_47db:;
    m.set_ah(uint8_t(0x0));
    goto branch_471d;
branch_47e0:;
    {
        const uint16_t callback_cs = m.cs();
        initialize_field_dialogue_overlay_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    goto branch_469b;
branch_47e8:;
    clear_field_overlay_core(m);
    goto branch_469b;
branch_47f0:;
    {
        const uint16_t callback_cs = m.cs();
        read_dialogue_frame_byte_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_ah(uint8_t(0x0));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | m.al()))));
    if (!(m.flags() & emu::ZF))
        goto branch_47fb;
    m.set_ah(uint8_t(computed::increment<uint8_t>(m, m.ah())));
branch_47fb:;
    m.cx() = uint16_t(m.ax());
    {
        const uint16_t callback_cs = m.cs();
        wait_field_frames_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    goto branch_469b;
branch_4805:;
    initialize_map_npcs_core(m);
    update_field_frame_core(m);
    goto branch_469b;
branch_4812:;
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    computed::push(m, m.dx());
    {
        const uint16_t callback_cs = m.cs();
        read_dialogue_frame_byte_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(computed::shift_right<uint8_t>(m, m.al(), 0x4)));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() & 0xF)));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.ax()));
    m.bx() = uint16_t(m.ax());
    m.bx() = uint16_t(m.rd16(m.cs(), uint16_t(m.bx() + 0x4836)));
    m.set_al(uint8_t(uint8_t(m.cx())));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() & 0xF)));
    {
        const uint16_t callback_cs = m.cs();
        dispatch_computed_handler_core(m, m.bx());
        if (m.cs() != callback_cs)
            return;
    }
    m.dx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    goto branch_469b;
branch_49a0:;
    {
        const uint16_t callback_cs = m.cs();
        wait_field_dialogue_acknowledgement_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    goto branch_469b;
branch_49a8:;
    {
        const uint16_t callback_cs = m.cs();
        pop_dialogue_frame_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(0xFFFF);
    goto branch_471d;
}
void read_field_dialogue_token(Machine &m) {
    read_field_dialogue_token_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x35483E (near)
// Skip three script bytes when the selected packed counter is outside the encoded range.
void dialogue_test_counter_range_core(Machine &m) {
    {
        const uint16_t callback_cs = m.cs();
        read_packed_event_counter_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.al()));
    {
        const uint16_t callback_cs = m.cs();
        read_dialogue_frame_byte_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.dx() = uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.set_al(uint8_t(computed::shift_right<uint8_t>(m, m.al(), 0x4)));
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.dx()));
    if (m.flags() & emu::ZF)
        goto branch_4851;
    if (!(m.flags() & emu::CF))
        goto branch_4859;
branch_4851:;
    m.dx() =
        uint16_t((m.dx() & 0x00FF) |
                 (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.dx() >> 8) & 0xF)))) << 8));
    (void)computed::subtract<uint8_t>(m, uint8_t(m.dx() >> 8), uint8_t(m.dx()));
    if (m.flags() & emu::CF)
        goto branch_4859;
    return;
branch_4859:;
    {
        const uint16_t callback_cs = m.cs();
        read_dialogue_frame_byte_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        read_dialogue_frame_byte_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        read_dialogue_frame_byte_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    return;
}
void dialogue_test_counter_range(Machine &m) {
    dialogue_test_counter_range_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x354863 (near)
// Apply a signed HP adjustment to the members selected by AL's bitmask.
void dialogue_adjust_party_hp_core(Machine &m) {
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.al()));
    {
        const uint16_t callback_cs = m.cs();
        read_dialogue_frame_byte_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    (void)computed::logic<uint8_t>(m, uint8_t(uint8_t(m.dx()) & 0x1));
    if (m.flags() & emu::ZF)
        goto branch_4873;
    m.bx() = uint16_t(kGameState);
    {
        const uint16_t callback_cs = m.cs();
        dialogue_adjust_member_hp_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_4873:;
    (void)computed::logic<uint8_t>(m, uint8_t(uint8_t(m.dx()) & 0x2));
    if (m.flags() & emu::ZF)
        goto branch_487e;
    m.bx() = uint16_t(kMember1Record);
    {
        const uint16_t callback_cs = m.cs();
        dialogue_adjust_member_hp_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_487e:;
    (void)computed::logic<uint8_t>(m, uint8_t(uint8_t(m.dx()) & 0x4));
    if (m.flags() & emu::ZF)
        goto branch_4889;
    m.bx() = uint16_t(kMember2Record);
    {
        const uint16_t callback_cs = m.cs();
        dialogue_adjust_member_hp_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_4889:;
    (void)computed::logic<uint8_t>(m, uint8_t(uint8_t(m.dx()) & 0x8));
    if (m.flags() & emu::ZF)
        goto branch_4894;
    m.bx() = uint16_t(kMember3Record);
    {
        const uint16_t callback_cs = m.cs();
        dialogue_adjust_member_hp_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_4894:;
    return;
}
void dialogue_adjust_party_hp(Machine &m) {
    dialogue_adjust_party_hp_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x354895 (near)
// Adjust a living member's HP and clamp it between one and maximum HP.
void dialogue_adjust_member_hp_core(Machine &m) {
    computed::push(m, m.ax());
    computed::push(m, m.dx());
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.bx()))));
    (void)computed::logic<uint8_t>(m, uint8_t(m.al() & 0x1));
    if (!(m.flags() & emu::ZF))
        goto branch_48c3;
    m.dx() = uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(0x0)) << 8));
    (void)computed::logic<uint8_t>(m, uint8_t(uint8_t(m.dx()) & 0x80));
    if (m.flags() & emu::ZF)
        goto branch_48a9;
    m.dx() = uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(0xFF)) << 8));
branch_48a9:;
    // Sign-extend the script byte before adding it to HP, then clamp a living member to [1, max HP].
    if (int16_t(m.dx()) < 0) // a loss: FieldDamage may lower it (0 keeps the HP)
        m.dx() =
            uint16_t(-hooks::filter(hooks::Value::FieldDamage, m, -int16_t(m.dx()), {1, int32_t(m.bx())}));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.rd16(m.ss(), uint16_t(m.bx() + 0x0006))));
    (void)computed::subtract<uint16_t>(m, m.dx(), 0x1);
    if (bool(m.flags() & emu::SF) == bool(m.flags() & emu::OF))
        goto branch_48b5;
    m.dx() = uint16_t(0x1);
branch_48b5:;
    (void)computed::subtract<uint16_t>(m, m.dx(), m.rd16(m.ss(), uint16_t(m.bx() + 0x0008)));
    if (m.flags() & emu::CF)
        goto branch_48bf;
    m.dx() = uint16_t(m.rd16(m.ss(), uint16_t(m.bx() + 0x0008)));
branch_48bf:;
    m.wr16(m.ss(), uint16_t(m.bx() + 0x0006), uint16_t(m.dx()));
branch_48c3:;
    m.dx() = uint16_t(computed::pop(m));
    m.ax() = uint16_t(computed::pop(m));
    return;
}
void dialogue_adjust_member_hp(Machine &m) {
    dialogue_adjust_member_hp_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x3548C6 (near)
// Skip three script bytes when the requested item is absent from the party.
void dialogue_test_owned_item_core(Machine &m) {
    goto entry_48c6;
branch_4859:;
    {
        const uint16_t callback_cs = m.cs();
        read_dialogue_frame_byte_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        read_dialogue_frame_byte_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        read_dialogue_frame_byte_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    return;
entry_48c6:;
    {
        const uint16_t callback_cs = m.cs();
        read_dialogue_frame_byte_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.al()));
    {
        const uint16_t callback_cs = m.cs();
        find_owned_dialogue_item_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    if (!(m.flags() & emu::ZF))
        goto branch_4859;
    return;
}
void dialogue_test_owned_item(Machine &m) {
    dialogue_test_owned_item_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x3548D1 (near)
// Search all equipment, inventory and overflow slots for DL's item.
void find_owned_dialogue_item_core(Machine &m) {
    m.bx() = uint16_t(kMember0Equipment);
    {
        const uint16_t callback_cs = m.cs();
        find_item_in_eight_slots_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    if (m.flags() & emu::ZF)
        goto branch_48ff;
    m.bx() = uint16_t((kMember0Equipment + 0x1F));
    {
        const uint16_t callback_cs = m.cs();
        find_item_in_eight_slots_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    if (m.flags() & emu::ZF)
        goto branch_48ff;
    m.bx() = uint16_t((kMember0Equipment + 0x3E));
    {
        const uint16_t callback_cs = m.cs();
        find_item_in_eight_slots_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    if (m.flags() & emu::ZF)
        goto branch_48ff;
    m.bx() = uint16_t((kMember0Equipment + 0x5D));
    {
        const uint16_t callback_cs = m.cs();
        find_item_in_eight_slots_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    if (m.flags() & emu::ZF)
        goto branch_48ff;
    m.bx() = uint16_t(kInventory);
    {
        const uint16_t callback_cs = m.cs();
        find_item_in_eight_slots_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    if (m.flags() & emu::ZF)
        goto branch_48ff;
    m.bx() = uint16_t(kKeyItems);
    {
        const uint16_t callback_cs = m.cs();
        find_item_in_nine_slots_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_48ff:;
    return;
}
void find_owned_dialogue_item(Machine &m) {
    find_owned_dialogue_item_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x354900 (near)
// Test eight two-byte item slots for DL and return equality in flags.
void find_item_in_eight_slots_core(Machine &m) {
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x8));
branch_4904:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.dx()), m.rd8(m.ss(), uint16_t(m.bx())));
    if (m.flags() & emu::ZF)
        goto branch_4914;
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    if (!(m.flags() & emu::ZF))
        goto branch_4904;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1));
    m.cx() = uint16_t((m.cx() & 0xFF00) |
                      uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.cx()) | uint8_t(m.cx())))));
branch_4914:;
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    return;
}
void find_item_in_eight_slots(Machine &m) {
    find_item_in_eight_slots_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x354917 (near)
// Test nine two-byte overflow slots for DL and return equality in flags.
void find_item_in_nine_slots_core(Machine &m) {
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x9));
branch_491b:;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.dx()), m.rd8(m.ss(), uint16_t(m.bx())));
    if (m.flags() & emu::ZF)
        goto branch_492b;
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), 0x2));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    if (!(m.flags() & emu::ZF))
        goto branch_491b;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1));
    m.cx() = uint16_t((m.cx() & 0xFF00) |
                      uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.cx()) | uint8_t(m.cx())))));
branch_492b:;
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    return;
}
void find_item_in_nine_slots(Machine &m) {
    find_item_in_nine_slots_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x35492E (near)
// Set or saturating-add the encoded value to a packed event counter.
void dialogue_update_packed_counter_core(Machine &m) {
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    {
        const uint16_t callback_cs = m.cs();
        read_dialogue_frame_byte_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.al()));
    m.dx() =
        uint16_t((m.dx() & 0xFF00) | uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.dx()) & 0xF))));
    m.set_al(uint8_t(computed::shift_right<uint8_t>(m, m.al(), 0x4)));
    m.dx() = uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    {
        const uint16_t callback_cs = m.cs();
        read_packed_event_counter_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    (void)computed::subtract<uint8_t>(m, uint8_t(m.cx()), 0x0);
    if (m.flags() & emu::ZF)
        goto branch_494e;
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(computed::add<uint8_t>(m, uint8_t(m.dx()), m.al())));
    (void)computed::subtract<uint8_t>(m, uint8_t(m.dx()), 0x10);
    if (m.flags() & emu::CF)
        goto branch_494e;
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0xF));
branch_494e:;
    m.set_al(uint8_t(uint8_t(m.dx() >> 8)));
    {
        const uint16_t callback_cs = m.cs();
        write_packed_event_counter_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    return;
}
void dialogue_update_packed_counter(Machine &m) {
    dialogue_update_packed_counter_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x354954 (near)
// Return the selected event counter nibble in AL.
void read_packed_event_counter_core(Machine &m) {
    computed::push(m, m.bx());
    computed::push(m, m.ax());
    m.bx() = uint16_t(m.ax());
    m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() & 0xF)));
    m.bx() = uint16_t(computed::shift_right<uint16_t>(m, m.bx(), 0x1));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.bx() + kPackedProgress))));
    m.ax() = uint16_t(computed::pop(m));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x1))));
    if (!(m.flags() & emu::ZF))
        goto branch_496b;
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::shift_right<uint8_t>(m, uint8_t(m.bx()), 0x4)));
branch_496b:;
    m.set_al(uint8_t(uint8_t(m.bx())));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0xF))));
    m.bx() = uint16_t(computed::pop(m));
    return;
}
void read_packed_event_counter(Machine &m) {
    read_packed_event_counter_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x354971 (near)
// Store DL in the selected event counter nibble.
void write_packed_event_counter_core(Machine &m) {
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    computed::push(m, m.ax());
    m.bx() = uint16_t(m.ax());
    m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() & 0xF)));
    m.bx() = uint16_t(computed::shift_right<uint16_t>(m, m.bx(), 0x1));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.bx() + kPackedProgress))));
    m.ax() = uint16_t(computed::pop(m));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x1))));
    if (!(m.flags() & emu::ZF))
        goto branch_4990;
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(computed::shift_left<uint8_t>(m, uint8_t(m.dx()), 0x4)));
    m.cx() =
        uint16_t((m.cx() & 0xFF00) | uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.cx()) & 0xF))));
    m.cx() = uint16_t((m.cx() & 0xFF00) |
                      uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.cx()) | uint8_t(m.dx())))));
    goto branch_4998;
branch_4990:;
    m.dx() =
        uint16_t((m.dx() & 0xFF00) | uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.dx()) & 0xF))));
    m.cx() =
        uint16_t((m.cx() & 0xFF00) | uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.cx()) & 0xF0))));
    m.cx() = uint16_t((m.cx() & 0xFF00) |
                      uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.cx()) | uint8_t(m.dx())))));
branch_4998:;
    m.wr8(m.ss(), uint16_t(m.bx() + kPackedProgress), uint8_t(uint8_t(m.cx())));
    m.bx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    return;
}
void write_packed_event_counter(Machine &m) {
    write_packed_event_counter_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x3549B1
// Animate the dialogue prompt sprite until A is pressed.
void wait_field_dialogue_acknowledgement_core(Machine &m) {
    hooks::emit(hooks::Event::Dialogue, m, {3, 1});
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kDialogueSkipDelay), uint8_t(m.al()));
    m.ax() = uint16_t(0x9000);
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(0x3418);
    m.ax() = uint16_t(0x1F3);
    m.cx() = uint16_t(0x8);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
branch_49ce:;
    m.dx() = uint16_t(m.rd16(m.ss(), uint16_t(kFrames)));
    m.dx() = uint16_t(computed::shift_right<uint16_t>(m, m.dx(), 0x5));
    m.dx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.dx() & 0x1)));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.dx()));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), m.dx()));
    m.dx() = uint16_t(computed::add<uint16_t>(m, m.dx(), 0x2FF3));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0xD0));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x7C)) << 8));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_sprite_16x16_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    update_field_frame_core(m);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        m.call_far(0x4000, 0x005C);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kKeysPressed)));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x4)));
    if (m.flags() & emu::ZF)
        goto branch_49ce;
    update_field_frame_core(m);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    hooks::emit(hooks::Event::Dialogue, m, {3, 0});
    return;
}
void wait_field_dialogue_acknowledgement(Machine &m) {
    wait_field_dialogue_acknowledgement_core(m);
    if (m.cs() == 0x5000)
        m.retf();
}
// rom: 0x354A08 (near)
// Return the current eight-byte dialogue stack record in SI.
void current_dialogue_frame_pointer_core(Machine &m) {
    computed::push(m, m.ds());
    computed::push(m, m.ax());
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kInteractionState))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | m.al()))));
    if (m.flags() & emu::ZF)
        goto branch_4a1a;
    m.ax() = uint16_t(computed::increment<uint16_t>(m, m.ax(), true));
branch_4a1a:;
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x3));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), kDialogueFrames));
    m.si() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    return;
}
void current_dialogue_frame_pointer(Machine &m) {
    current_dialogue_frame_pointer_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x354A25 (near)
// Store DX as the current dialogue frame's read cursor.
void set_dialogue_frame_cursor_core(Machine &m) {
    computed::push(m, m.ds());
    computed::push(m, m.ax());
    computed::push(m, m.si());
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    {
        const uint16_t callback_cs = m.cs();
        current_dialogue_frame_pointer_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.wr16(m.ss(), uint16_t(m.si()), uint16_t(m.dx()));
    m.si() = uint16_t(computed::pop(m));
    m.ax() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    return;
}
void set_dialogue_frame_cursor(Machine &m) {
    set_dialogue_frame_cursor_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x354A37 (near)
// Decrement the dialogue stack depth.
void pop_dialogue_frame_core(Machine &m) {
    computed::push(m, m.ds());
    computed::push(m, m.ax());
    computed::push(m, m.si());
    m.ax() = uint16_t(0x0);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kInteractionState))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ss(), uint16_t(kInteractionState), uint8_t(m.al()));
    m.si() = uint16_t(computed::pop(m));
    m.ax() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    return;
}
void pop_dialogue_frame(Machine &m) {
    pop_dialogue_frame_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x354A4F (near)
// Look up AL's dialogue stream and push its segment and cursor.
void push_indexed_dialogue_frame_core(Machine &m) {
    computed::push(m, m.ax());
    computed::push(m, m.bx());
    computed::push(m, m.dx());
    computed::push(m, m.ds());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() & 0xFF)));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.ax()));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), 0x7B62));
    m.bx() = uint16_t(m.ax());
    m.dx() = uint16_t(m.rd16(m.cs(), uint16_t(m.bx())));
    m.ax() = uint16_t(m.cs());
    m.ds() = uint16_t(m.ax());
    {
        const uint16_t callback_cs = m.cs();
        push_dialogue_frame_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ds() = uint16_t(computed::pop(m));
    m.dx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    m.ax() = uint16_t(computed::pop(m));
    return;
}
void push_indexed_dialogue_frame(Machine &m) {
    push_indexed_dialogue_frame_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x354A6C (near)
// Push DS:DX onto the dialogue stack and clear its token-expansion state.
void push_dialogue_frame_core(Machine &m) {
    computed::push(m, m.ds());
    computed::push(m, m.ax());
    computed::push(m, m.si());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kInteractionState))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    m.wr8(m.ss(), uint16_t(kInteractionState), uint8_t(m.al()));
    {
        const uint16_t callback_cs = m.cs();
        current_dialogue_frame_pointer_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.wr16(m.ss(), uint16_t(m.si()), uint16_t(m.dx()));
    m.ax() = uint16_t(m.ds());
    m.wr16(m.ss(), uint16_t(m.si() + 0x0002), uint16_t(m.ax()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr8(m.ss(), uint16_t(m.si() + 0x0004), uint8_t(m.al()));
    m.si() = uint16_t(computed::pop(m));
    m.ax() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    return;
}
void push_dialogue_frame(Machine &m) {
    push_dialogue_frame_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x354A91 (near)
// Read AL from the current dialogue stream and advance its cursor.
void read_dialogue_frame_byte_core(Machine &m) {
    computed::push(m, m.ds());
    computed::push(m, m.bx());
    computed::push(m, m.si());
    {
        const uint16_t callback_cs = m.cs();
        current_dialogue_frame_pointer_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    if (Session *session = Session::find(m.wsc())) // a loaded language's dialogue (text.h)
        session->dialogue_frame(m, m.si());
    m.bx() = uint16_t(m.rd16(m.ss(), uint16_t(m.si())));
    m.ds() = uint16_t(m.rd16(m.ss(), uint16_t(m.si() + 0x0002)));
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.bx()))));
    m.bx() = uint16_t(computed::increment<uint16_t>(m, m.bx()));
    m.wr16(m.ss(), uint16_t(m.si()), uint16_t(m.bx()));
    m.si() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    return;
}
void read_dialogue_frame_byte(Machine &m) {
    read_dialogue_frame_byte_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x357D60 (near)
// Test whether AL is one of the unique story-item IDs.
void test_unique_story_item_core(Machine &m) {
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1E);
    if (m.flags() & emu::ZF)
        goto branch_7d73;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1F);
    if (m.flags() & emu::ZF)
        goto branch_7d73;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x11);
    if (m.flags() & emu::CF)
        goto branch_7d70;
    (void)computed::subtract<uint8_t>(m, m.al(), 0x1B);
    if (m.flags() & emu::CF)
        goto branch_7d73;
branch_7d70:;
    (void)computed::subtract<uint8_t>(m, m.al(), m.al());
    return;
branch_7d73:;
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | m.al()))));
    return;
}
void test_unique_story_item(Machine &m) {
    test_unique_story_item_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x357DEA (near)
// Set AL's bit in the acquired-item flags.
void mark_item_acquired_core(Machine &m) {
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() ^ m.bx())));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::shift_right<uint8_t>(m, uint8_t(m.bx()), 0x3)));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x7))));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(0x1));
    m.set_al(uint8_t(computed::rotate_left<uint8_t>(m, m.al(), uint8_t(m.cx()))));
    m.wr8(m.ss(), uint16_t(m.bx() + kBestiaryFlags),
          uint8_t(computed::logic<uint8_t>(
              m, uint8_t(m.rd8(m.ss(), uint16_t(m.bx() + kBestiaryFlags)) | m.al()))));
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    return;
}
void mark_item_acquired(Machine &m) {
    mark_item_acquired_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x357E46 (near)
// Require the player to discard one inventory item when the inventory overflows.
void discard_overflow_inventory_item_core(Machine &m) {
    hooks::EventScope presentation(m, hooks::Event::ItemDialog, {4, 2});
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.ax() = uint16_t(0x2A);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x9000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(0x80);
    m.cx() = uint16_t(0x20);
    m.si() = uint16_t(0x2480);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        copy_tiles_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(m.ss());
    m.es() = uint16_t(m.ax());
    m.di() = uint16_t(kEffectDistortion);
    m.cx() = uint16_t(0x50);
    m.set_al(uint8_t(0xFF));
    computed::fill_bytes(m);
    m.si() = uint16_t(kInventory);
    m.di() = uint16_t(kEffectDistortion);
    m.bp() = uint16_t(kOverflowItemUsesText);
    m.cx() = uint16_t(0x8);
branch_7e7f:;
    computed::push(m, m.cx());
    computed::push(m, m.si());
    computed::push(m, m.di());
    computed::push(m, m.bp());
    m.ax() = uint16_t(0x1FD9);
    m.wr16(m.ss(), uint16_t(m.bp()), uint16_t(m.ax()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0001))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_7ec9;
    computed::push(m, m.si());
    m.ax() = uint16_t(0x9000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x3));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x4BE0));
    if (const TextPtr name = name_record(m, NameTable::Item, uint8_t(m.ax() >> 3));
        name.segment == kTextSegment) {
        m.ds() = name.segment; // a loaded language's name (text.h)
        m.si() = name.offset;
    }
    m.cx() = uint16_t(0x4);
    computed::copy_words(m);
    m.si() = uint16_t(computed::pop(m));
    m.ax() = uint16_t(0x9000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_7ec9;
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x6EE0));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si()))));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), 0x1E80));
    m.wr16(m.ss(), uint16_t(m.bp()), uint16_t(m.ax()));
branch_7ec9:;
    m.bp() = uint16_t(computed::pop(m));
    m.di() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0x2));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x8));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_7e7f;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr16(m.ss(), uint16_t(kSavedSceneSp), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(kSceneIndex), uint16_t(m.ax()));
    update_field_frame_core(m);
    m.ax() = uint16_t(0x2);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x0));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xD)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x1C));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x5)) << 8));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        draw_window_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(0x2);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x11));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xD)) << 8));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0xB));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(0x5)) << 8));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        draw_window_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    {
        const TextPtr text_ptr = text(m, TextKey::discard_overflow_inventory_item_1);
        m.ds() = text_ptr.segment;
        m.si() = text_ptr.offset;
        m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x3));
        m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xE)) << 8));
        m.ax() = uint16_t(0x2);
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x4000;
            draw_text_core(m);
            if (m.cs() != 0x4000)
                return;
            m.cs() = caller_cs;
        }
        text_done(m, TextKey::discard_overflow_inventory_item_1);
    }
    {
        const TextPtr text_ptr = text(m, TextKey::discard_overflow_inventory_item_2);
        m.ds() = text_ptr.segment;
        m.si() = text_ptr.offset;
        m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x3));
        m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xF)) << 8));
        m.ax() = uint16_t(0x2);
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x4000;
            draw_text_core(m);
            if (m.cs() != 0x4000)
                return;
            m.cs() = caller_cs;
        }
        text_done(m, TextKey::discard_overflow_inventory_item_2);
    }
branch_7f2a:;
    {
        const uint16_t callback_cs = m.cs();
        draw_overflow_inventory_page_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        draw_overflow_inventory_cursor_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    update_field_frame_core(m);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        m.call_far(0x4000, 0x005C);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kKeysPressed)));
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x4));
    if (!(m.flags() & emu::ZF))
        goto branch_7f53;
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kKeysRepeat)));
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x10));
    if (!(m.flags() & emu::ZF))
        goto branch_7f79;
    (void)computed::logic<uint16_t>(m, uint16_t(m.ax() & 0x40));
    if (!(m.flags() & emu::ZF))
        goto branch_7f9e;
    goto branch_7f2a;
branch_7f53:;
    m.si() = uint16_t(m.rd16(m.ss(), uint16_t(kSavedSceneSp)));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.rd16(m.ss(), uint16_t(kSceneIndex))));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), m.si()));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), kInventory));
    m.set_al(uint8_t(0xFF));
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(m.si() + 0x0001), uint8_t(m.al()));
    m.ax() = uint16_t(0x93);
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
branch_7f79:;
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kSavedSceneSp)));
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x1)));
    if (!(m.flags() & emu::CF))
        goto branch_7f90;
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kSceneIndex)));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | m.ax())));
    if (m.flags() & emu::ZF)
        goto branch_7f2a;
    m.ax() = uint16_t(computed::increment<uint16_t>(m, m.ax(), true));
    m.wr16(m.ss(), uint16_t(kSceneIndex), uint16_t(m.ax()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
branch_7f90:;
    m.wr16(m.ss(), uint16_t(kSavedSceneSp), uint16_t(m.ax()));
    m.ax() = uint16_t(0x92);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    goto branch_7f2a;
branch_7f9e:;
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kSavedSceneSp)));
    m.ax() = uint16_t(computed::increment<uint16_t>(m, m.ax()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x3);
    if (m.flags() & emu::CF)
        goto branch_7f90;
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kSceneIndex)));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x5);
    if (m.flags() & emu::CF)
        goto branch_7fb2;
    goto branch_7f2a;
branch_7fb2:;
    m.ax() = uint16_t(computed::increment<uint16_t>(m, m.ax()));
    m.wr16(m.ss(), uint16_t(kSceneIndex), uint16_t(m.ax()));
    m.ax() = uint16_t(0x2);
    goto branch_7f90;
}
void discard_overflow_inventory_item(Machine &m) {
    discard_overflow_inventory_item_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x357FBC (near)
// Draw the discard cursor and inventory scroll arrows.
void draw_overflow_inventory_cursor_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.rd8(m.ss(), uint16_t(kSavedSceneSp)))) << 8));
    m.bx() = uint16_t((m.bx() & 0x00FF) |
                      (uint16_t(uint8_t(computed::shift_left<uint8_t>(m, uint8_t(m.bx() >> 8), 0x3))) << 8));
    m.bx() = uint16_t((m.bx() & 0x00FF) |
                      (uint16_t(uint8_t(computed::add<uint8_t>(m, uint8_t(m.bx() >> 8), 0x72))) << 8));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x88));
    m.dx() = uint16_t(0x3FFB);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_sprite_16x16_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kSceneIndex)));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() | m.ax())));
    if (m.flags() & emu::ZF)
        goto branch_7fe9;
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0xB0));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x68)) << 8));
    m.dx() = uint16_t(0x3FF0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_sprite_8x8_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
branch_7fe9:;
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kSceneIndex)));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x5);
    if (m.flags() & emu::ZF)
        goto branch_7ffd;
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0xB0));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0x88)) << 8));
    m.dx() = uint16_t(0x3FF1);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        append_sprite_8x8_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
branch_7ffd:;
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void draw_overflow_inventory_cursor(Machine &m) {
    draw_overflow_inventory_cursor_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x358002 (near)
// Draw three inventory names and icons at the current discard-menu scroll offset.
void draw_overflow_inventory_page_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.ax() = uint16_t(m.ss());
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(kSceneIndex)));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.ax()));
    m.bp() = uint16_t(m.ax());
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), kOverflowItemUsesText));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.ax()));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.ax()));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), kEffectDistortion));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x13));
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xE)) << 8));
    m.cx() = uint16_t(0x3);
branch_8027:;
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    computed::push(m, m.si());
    computed::push(m, m.bp());
    m.ax() = uint16_t(0x2);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        screen_cell_pointer_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(m.rd16(m.ss(), uint16_t(m.bp())));
    m.wr16(m.ss(), uint16_t(m.di()), uint16_t(m.ax()));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.bx()))));
    m.ax() = uint16_t(0x2);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        draw_text_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bp() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), 0x2));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x8));
    m.bx() = uint16_t((m.bx() & 0x00FF) |
                      (uint16_t(uint8_t(computed::increment<uint8_t>(m, uint8_t(m.bx() >> 8)))) << 8));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_8027;
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void draw_overflow_inventory_page(Machine &m) {
    draw_overflow_inventory_page_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x358057 (near)
// Put the event's item in the first free inventory slot, returning carry if full.
void grant_field_inventory_item_core(Machine &m) {
    m.si() = uint16_t(kInventory);
    m.cx() = uint16_t(0x8);
branch_805d:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0001))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_806d;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_805d;
    m.flags() |= emu::CF;
    return;
branch_806d:;
    computed::push(m, m.ds());
    m.ax() = uint16_t(0x9000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldEventArgument))));
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() ^ m.bx())));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), m.bx()));
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), 0x41E0));
    m.bx() = uint16_t(m.rd16(m.ds(), uint16_t(m.bx())));
    m.set_al(uint8_t(table_at(m, Table::item_records, m.ds(), uint16_t(m.bx() + 0x0003))));
    m.wr8(m.ss(), uint16_t(m.si() + 0x0001), uint8_t(m.al()));
    m.ds() = uint16_t(computed::pop(m));
    m.flags() &= ~emu::CF;
    return;
}
void grant_field_inventory_item(Machine &m) {
    grant_field_inventory_item_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x358092 (near)
// Store the event's unique item in an empty overflow slot unless already present.
void grant_unique_story_item_core(Machine &m) {
    m.si() = uint16_t(kKeyItems);
    m.cx() = uint16_t(0x9);
branch_8098:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    (void)computed::subtract<uint8_t>(m, m.al(), m.rd8(m.ss(), uint16_t(kFieldEventArgument)));
    if (m.flags() & emu::ZF)
        goto branch_80e1;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_8098;
    m.si() = uint16_t(kKeyItems);
    m.cx() = uint16_t(0x9);
branch_80ae:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0001))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_80be;
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_80ae;
    goto branch_80e1;
branch_80be:;
    computed::push(m, m.ds());
    m.ax() = uint16_t(0x9000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldEventArgument))));
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() ^ m.bx())));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), m.bx()));
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), 0x41E0));
    m.bx() = uint16_t(m.rd16(m.ds(), uint16_t(m.bx())));
    m.set_al(uint8_t(table_at(m, Table::item_records, m.ds(), uint16_t(m.bx() + 0x0003))));
    m.wr8(m.ss(), uint16_t(m.si() + 0x0001), uint8_t(m.al()));
    m.ds() = uint16_t(computed::pop(m));
branch_80e1:;
    m.flags() &= ~emu::CF;
    return;
}
void grant_unique_story_item(Machine &m) {
    grant_unique_story_item_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x358167 (near)
// Show the event's dialogue while temporarily disabling ordinary field movement.
void event_show_dialogue_overlay_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBlinkEnabled))));
    computed::push(m, m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr16(m.ss(), uint16_t(kMoveDelta), uint16_t(m.ax()));
    m.wr8(m.ss(), uint16_t(kBlinkEnabled), uint8_t(m.al()));
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(kAnimationStep), uint8_t(m.al()));
    m.set_ah(uint8_t(m.rd8(m.ss(), uint16_t(kFieldEventKind))));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldEventArgument))));
    {
        const uint16_t callback_cs = m.cs();
        show_field_dialogue_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(computed::pop(m));
    m.wr8(m.ss(), uint16_t(kBlinkEnabled), uint8_t(m.al()));
    return;
}
void event_show_dialogue_overlay(Machine &m) {
    event_show_dialogue_overlay_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x35818E (near)
// Process the event's dialogue without opening a new overlay.
void event_continue_dialogue_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBlinkEnabled))));
    computed::push(m, m.ax());
    m.set_al(uint8_t(0x0));
    m.wr8(m.ss(), uint16_t(kBlinkEnabled), uint8_t(m.al()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldEventArgument))));
    {
        const uint16_t callback_cs = m.cs();
        interpret_field_dialogue_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(computed::pop(m));
    m.wr8(m.ss(), uint16_t(kBlinkEnabled), uint8_t(m.al()));
    return;
}
void event_continue_dialogue(Machine &m) {
    event_continue_dialogue_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x3581A8 (near)
// Grant an uncollected event item and display its acquired-item message.
void event_grant_item_with_message_core(Machine &m) {
    TextPtr suffix{};
    m.wr8(m.ss(), uint16_t(kFieldEventPending), uint8_t(m.al()));
    read_bestiary_bit_call_core(m);
    if (m.flags() & emu::ZF)
        goto branch_81b4;
    goto branch_8259;
branch_81b4:;
    {
        const uint16_t callback_cs = m.cs();
        initialize_field_dialogue_overlay_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldEventArgument))));
    {
        const uint16_t callback_cs = m.cs();
        test_unique_story_item_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    if (!(m.flags() & emu::ZF))
        goto branch_81e8;
    {
        const uint16_t callback_cs = m.cs();
        grant_field_inventory_item_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    if (!(m.flags() & emu::CF))
        goto branch_81eb;
    computed::push(m, m.ds());
    {
        const TextPtr text_ptr = text(m, TextKey::event_grant_item_with_message_1);
        m.ds() = text_ptr.segment;
        m.si() = text_ptr.offset;
        m.ax() = uint16_t(0x2);
        m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xE)) << 8));
        m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x3));
        // The host box shows the whole message as the native one starts typing.
        hooks::emit(hooks::Event::Dialogue, m, {5, 1, m.rd8(m.ss(), uint16_t(kFieldEventArgument))});
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x4000;
            draw_text_with_key_skip_core(m);
            if (m.cs() != 0x4000)
                return;
            m.cs() = caller_cs;
        }
        text_done(m, TextKey::event_grant_item_with_message_1);
    }
    hooks::emit(hooks::Event::Dialogue, m, {5, 1, m.rd8(m.ss(), uint16_t(kFieldEventArgument))});
    m.ds() = uint16_t(computed::pop(m));
    {
        const uint16_t callback_cs = m.cs();
        wait_field_dialogue_acknowledgement_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    clear_field_overlay_core(m);
    goto branch_8259;
branch_81e8:;
    {
        const uint16_t callback_cs = m.cs();
        grant_unique_story_item_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_81eb:;
    // The item is granted: the host box shows "obtained" at once, not after the native message has typed out
    // behind it (presentation only, the native pacing below is unchanged).
    hooks::emit(hooks::Event::Dialogue, m, {5, 0, m.rd8(m.ss(), uint16_t(kFieldEventArgument))});
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldEventPending))));
    {
        const uint16_t callback_cs = m.cs();
        mark_item_acquired_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    initialize_map_npcs_core(m);
    update_field_frame_core(m);
    m.ax() = uint16_t(0x17);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    computed::push(m, m.ds());
    m.ax() = uint16_t(0x9000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldEventArgument))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x3));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), 0x4BE0));
    m.si() = uint16_t(m.ax());
    if (const TextPtr name = name_record(m, NameTable::Item, m.rd8(m.ss(), uint16_t(kFieldEventArgument)));
        name.segment == kTextSegment) {
        m.ds() = name.segment; // a loaded language's name (text.h)
        m.si() = name.offset;
    }
    m.di() = uint16_t(kLzDictionary);
branch_821b:;
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.si()))));
    m.wr8(m.ss(), uint16_t(m.di()), uint8_t(m.al()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xD9);
    if (m.flags() & emu::ZF)
        goto branch_822c;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    if (m.flags() & emu::ZF)
        goto branch_822c;
    m.si() = uint16_t(computed::increment<uint16_t>(m, m.si()));
    m.di() = uint16_t(computed::increment<uint16_t>(m, m.di()));
    goto branch_821b;
branch_822c:;
    suffix = text(m, TextKey::item_obtained_suffix); // D_358260 "　を　てにいれた"
    m.si() = suffix.offset;
branch_822f:;
    m.set_al(uint8_t(m.rd8(suffix.segment, uint16_t(m.si()))));
    m.wr8(m.ss(), uint16_t(m.di()), uint8_t(m.al()));
    m.si() = uint16_t(computed::increment<uint16_t>(m, m.si()));
    m.di() = uint16_t(computed::increment<uint16_t>(m, m.di()));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_822f;
    m.ax() = uint16_t(m.ss());
    m.ds() = uint16_t(m.ax());
    m.si() = uint16_t(kLzDictionary);
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(0xE)) << 8));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(0x3));
    m.ax() = uint16_t(0x2);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        draw_text_with_key_skip_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    hooks::emit(hooks::Event::Dialogue, m, {5, 0, m.rd8(m.ss(), uint16_t(kFieldEventArgument))});
    m.ds() = uint16_t(computed::pop(m));
    {
        const uint16_t callback_cs = m.cs();
        wait_field_dialogue_acknowledgement_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    clear_field_overlay_core(m);
branch_8259:;
    m.wr8(m.ss(), uint16_t(kFieldEventPending), uint8_t(0x0));
    return;
}
void event_grant_item_with_message(Machine &m) {
    event_grant_item_with_message_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x35827E (near)
// Grant the event item, requiring an inventory discard when full.
void event_grant_item_or_discard_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldEventArgument))));
    {
        const uint16_t callback_cs = m.cs();
        test_unique_story_item_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    if (!(m.flags() & emu::ZF))
        goto branch_829b;
branch_8287:;
    {
        const uint16_t callback_cs = m.cs();
        grant_field_inventory_item_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    if (!(m.flags() & emu::CF))
        goto branch_829e;
    {
        const uint16_t callback_cs = m.cs();
        initialize_field_dialogue_overlay_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t callback_cs = m.cs();
        discard_overflow_inventory_item_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    clear_field_overlay_core(m);
    goto branch_8287;
branch_829b:;
    {
        const uint16_t callback_cs = m.cs();
        grant_unique_story_item_core(m);
        if (m.cs() != callback_cs)
            return;
    }
branch_829e:;
    clear_field_overlay_core(m);
    return;
}
void event_grant_item_or_discard(Machine &m) {
    event_grant_item_or_discard_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x3582A4 (near)
// Dispatch the event's shop, inn, revival or recruitment service.
void event_open_service_menu_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldEventArgument))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xF0);
    if (m.flags() & emu::ZF)
        goto branch_82c5;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xF1);
    if (m.flags() & emu::ZF)
        goto branch_82d4;
    (void)computed::subtract<uint8_t>(m, m.al(), 0xF2);
    if (!(m.flags() & emu::CF))
        goto branch_82e3;
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x3)));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        run_shop_menu_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    goto branch_82f2;
branch_82c5:;
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        run_inn_menu_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    goto branch_82f2;
branch_82d4:;
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        run_revive_member_menu_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    goto branch_82f2;
branch_82e3:;
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0xF2)));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        run_recruitment_member_menu_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
branch_82f2:;
    m.ax() = uint16_t(uint16_t(m.in8(uint8_t(0x10)) | (uint16_t(m.in8(uint8_t(0x10 + 1))) << 8)));
    m.wr16(m.ss(), uint16_t(kFieldScroll), uint16_t(m.ax()));
    m.ax() = uint16_t(uint16_t(m.in8(uint8_t(0x12)) | (uint16_t(m.in8(uint8_t(0x12 + 1))) << 8)));
    m.wr16(m.ss(), uint16_t(kIntroOverlayProgress), uint16_t(m.ax()));
    return;
}
void event_open_service_menu(Machine &m) {
    event_open_service_menu_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x3582FF (near)
// Save the current map position and warp to a destination, or return to the saved position.
void event_change_map_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    update_field_frame_core(m);
    clear_field_overlay_core(m);
    fade_palettes_16_core(m);
    m.bx() = uint16_t((m.bx() & 0x00FF) | (uint16_t(uint8_t(m.rd8(m.ss(), uint16_t(kFieldEventKind)))) << 8));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kFieldEventArgument))));
    m.bx() =
        uint16_t((m.bx() & 0x00FF) |
                 (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx() >> 8) & 0x1)))) << 8));
    m.bx() =
        uint16_t((m.bx() & 0x00FF) |
                 (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx() >> 8) ^ 0x1)))) << 8));
    (void)computed::subtract<uint16_t>(m, m.bx(), 0x1FF);
    if (m.flags() & emu::ZF)
        goto branch_836a;
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), m.bx()));
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), m.bx()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kMapId))));
    m.wr8(m.ss(), uint16_t(kPreviousMap), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kPlayerCell))));
    m.wr8(m.ss(), uint16_t(kPreviousPlayerX), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kPlayerY))));
    m.wr8(m.ss(), uint16_t(kPreviousPlayerY), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.cs(), uint16_t(m.bx() + 0x8945))));
    m.wr8(m.ss(), uint16_t(kMapId), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.cs(), uint16_t(m.bx() + 0x8946))));
    m.wr8(m.ss(), uint16_t(kPlayerCell), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.cs(), uint16_t(m.bx() + 0x8947))));
    m.wr8(m.ss(), uint16_t(kPlayerY), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.cs(), uint16_t(m.bx() + 0x8948))));
    m.wr8(m.ss(), uint16_t(kPlayerFacing), uint8_t(m.al()));
    goto branch_8382;
branch_836a:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kPreviousMap))));
    m.wr8(m.ss(), uint16_t(kMapId), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kPreviousPlayerX))));
    m.wr8(m.ss(), uint16_t(kPlayerCell), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kPreviousPlayerY))));
    m.wr8(m.ss(), uint16_t(kPlayerY), uint8_t(m.al()));
branch_8382:;
    initialize_field_display_core(m);
    fade_field_to_target_core(m);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void event_change_map(Machine &m) {
    event_change_map_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x358404 (near)
// Decode the event's packed music, queued-effect or immediate-effect request.
void event_request_sound_core(Machine &m) {
    m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() ^ m.bx())));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kFieldEventArgument))));
    (void)computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx()) & 0x80));
    if (!(m.flags() & emu::ZF))
        goto branch_842f;
    (void)computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx()) & 0x40));
    if (!(m.flags() & emu::ZF))
        goto branch_8422;
    m.bx() =
        uint16_t((m.bx() & 0xFF00) | uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx()) & 0x1F))));
    m.set_al(uint8_t(m.rd8(m.cs(), uint16_t(m.bx() + 0x8445))));
    m.wr8(m.ss(), uint16_t(kMusicRequest), uint8_t(m.al()));
    return;
branch_8422:;
    m.bx() =
        uint16_t((m.bx() & 0xFF00) | uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx()) & 0x1F))));
    m.set_al(uint8_t(m.rd8(m.cs(), uint16_t(m.bx() + 0x8445))));
    m.wr8(m.ss(), uint16_t(kSfxRequest), uint8_t(m.al()));
    return;
branch_842f:;
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(uint8_t(m.bx())));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x3F))));
    computed::push(m, m.ss());
    m.ds() = uint16_t(computed::pop(m));
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
void event_request_sound(Machine &m) {
    event_request_sound_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x35845A (near)
// Set the encounter indices and enter a scripted field battle.
void event_start_battle_core(Machine &m) {
    m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() ^ m.bx())));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kFieldEventArgument))));
    m.wr16(m.ds(), uint16_t(kSavedSceneSp), uint16_t(m.bx()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEncounterVariant))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | m.al()))));
    if (!(m.flags() & emu::ZF))
        goto branch_8476;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.cs(), uint16_t(m.bx() + 0x92AD))));
branch_8476:;
    m.wr16(m.ss(), uint16_t(kSceneIndex), uint16_t(m.ax()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr16(m.ss(), uint16_t(kIntroStrip), uint16_t(m.ax()));
    m.wr16(m.ss(), uint16_t(kBattleSceneScratch), uint16_t(m.ax()));
    if (!enter_field_battle_core(m))
        return;
    return;
}
void event_start_battle(Machine &m) {
    event_start_battle_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x35848A (near)
// Copy a destination into the selected parked vehicle record and clear travel mode.
void event_relocate_vehicle_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() ^ m.bx())));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kFieldEventArgument))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::rotate_left<uint8_t>(m, uint8_t(m.bx()), 0x2)));
    m.bx() =
        uint16_t((m.bx() & 0xFF00) | uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx()) & 0x3))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.bx()), true)));
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), m.bx()));
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), m.bx()));
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), kVehicleLocations));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldEventKind))));
    m.set_al(uint8_t(computed::shift_right<uint8_t>(m, m.al(), 0x4)));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x50)));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.ax()));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.ax()));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), 0x8945));
    m.si() = uint16_t(m.ax());
    m.set_al(uint8_t(m.rd8(m.cs(), uint16_t(m.si()))));
    m.wr8(m.ss(), uint16_t(m.bx()), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.cs(), uint16_t(m.si() + 0x0001))));
    m.wr8(m.ss(), uint16_t(m.bx() + 0x0001), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.cs(), uint16_t(m.si() + 0x0002))));
    m.wr8(m.ss(), uint16_t(m.bx() + 0x0002), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.cs(), uint16_t(m.si() + 0x0003))));
    m.wr8(m.ss(), uint16_t(m.bx() + 0x0003), uint8_t(m.al()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr8(m.ss(), uint16_t(kTravelMode), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void event_relocate_vehicle(Machine &m) {
    event_relocate_vehicle_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x3584E2 (near)
// Board the selected vehicle or park it before stepping out.
void event_board_or_leave_vehicle_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldEventArgument))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | m.al()))));
    if (m.flags() & emu::ZF)
        goto branch_855c;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kTravelMode))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | m.al()))));
    if (m.flags() & emu::ZF)
        goto branch_84fb;
    goto branch_85c7;
branch_84fb:;
    force_player_step_core(m);
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldEventArgument))));
    m.set_al(uint8_t(computed::shift_right<uint8_t>(m, m.al(), 0x6)));
    m.wr8(m.ss(), uint16_t(kTravelMode), uint8_t(m.al()));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.ax()));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.ax()));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), kVehicleLocations));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(m.si() + 0x0001), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(m.si() + 0x0002), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(m.si() + 0x0003), uint8_t(m.al()));
    initialize_player_sprite_core(m);
    m.bx() = uint16_t(m.rd16(m.ss(), uint16_t(kNpcLastMatch)));
    m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() | m.bx())));
    if (m.flags() & emu::ZF)
        goto branch_853d;
    m.set_al(uint8_t(0xFF));
    m.wr8(m.ss(), uint16_t(m.bx()), uint8_t(m.al()));
branch_853d:;
    computed::push(m, m.ds());
    computed::push(m, m.es());
    m.ax() = uint16_t(m.ss());
    m.ds() = uint16_t(m.ax());
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t((kPaletteTarget + 0x100));
    m.di() = uint16_t((kPaletteBuffer + 0x100));
    m.cx() = uint16_t(0x80);
    computed::copy_words(m);
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    update_field_frame_core(m);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
branch_855c:;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kTravelMode))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | m.al()))));
    if (m.flags() & emu::ZF)
        goto branch_85c7;
    {
        const uint16_t callback_cs = m.cs();
        test_player_forward_cell_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    if (!(m.flags() & emu::ZF))
        goto branch_85c7;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kTravelMode))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.ax()));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.ax()));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), kVehicleLocations));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kMapId))));
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kPlayerCell))));
    m.wr8(m.ss(), uint16_t(m.si() + 0x0001), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kPlayerY))));
    m.wr8(m.ss(), uint16_t(m.si() + 0x0002), uint8_t(m.al()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr8(m.ss(), uint16_t(m.si() + 0x0003), uint8_t(m.al()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr8(m.ss(), uint16_t(kTravelMode), uint8_t(m.al()));
    spawn_parked_vehicle_call_core(m);
    initialize_player_sprite_core(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    m.ax() = uint16_t(m.ss());
    m.ds() = uint16_t(m.ax());
    m.es() = uint16_t(m.ax());
    m.si() = uint16_t((kPaletteTarget + 0x100));
    m.di() = uint16_t((kPaletteBuffer + 0x100));
    m.cx() = uint16_t(0x80);
    computed::copy_words(m);
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    update_field_frame_core(m);
    force_player_step_core(m);
branch_85c7:;
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void event_board_or_leave_vehicle(Machine &m) {
    event_board_or_leave_vehicle_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x3585CC (near)
// Apply a scripted movement or facing change to the player or selected NPC.
void event_move_or_turn_character_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldEventKind))));
    m.set_ah(uint8_t(m.al()));
    m.set_al(uint8_t(computed::shift_right<uint8_t>(m, m.al(), 0x4)));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xF);
    if (!(m.flags() & emu::ZF))
        goto branch_85e0;
    goto branch_869a;
branch_85e0:;
    computed::push(m, m.ax());
    {
        const uint16_t callback_cs = m.cs();
        wait_npc_movements_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(computed::pop(m));
    npc_record_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xFF);
    if (!(m.flags() & emu::ZF))
        goto branch_85f6;
    goto branch_8700;
branch_85f6:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0x0011))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x8))));
    if (m.flags() & emu::ZF)
        goto branch_8601;
    goto branch_8700;
branch_8601:;
    m.wr16(m.ss(), uint16_t(kNpcLastMatch), uint16_t(m.si()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldEventArgument))));
    m.set_ah(uint8_t(m.al()));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0xF))));
    if (m.flags() & emu::ZF)
        goto branch_8685;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() ^ m.bx())));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.ah()));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::rotate_left<uint8_t>(m, uint8_t(m.bx()), 0x2)));
    m.bx() =
        uint16_t((m.bx() & 0xFF00) | uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx()) & 0x3))));
    m.bx() = uint16_t(computed::shift_left<uint16_t>(m, m.bx(), 0x3));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBlinkEnabled))));
    computed::push(m, m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr16(m.ss(), uint16_t(kMoveDelta), uint16_t(m.ax()));
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(kAnimationStep), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kBlinkEnabled), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.cs(), uint16_t(m.bx() + 0x8665))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | 0xC0))));
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    m.ax() = uint16_t(m.rd16(m.cs(), uint16_t(m.bx() + 0x8667)));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.ax()));
    m.wr16(m.ss(), uint16_t(m.si() + 0x000A), uint16_t(m.ax()));
    m.ax() = uint16_t(m.rd16(m.cs(), uint16_t(m.bx() + 0x8669)));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.ax()));
    m.wr16(m.ss(), uint16_t(m.si() + 0x000C), uint16_t(m.ax()));
    m.wr8(m.ss(), uint16_t(m.si() + 0x000E), uint8_t(uint8_t(m.cx())));
    {
        const uint16_t callback_cs = m.cs();
        wait_npc_movements_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(computed::pop(m));
    m.wr8(m.ss(), uint16_t(kBlinkEnabled), uint8_t(m.al()));
    goto branch_8700;
branch_8685:;
    m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() ^ m.bx())));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.ah()));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::rotate_left<uint8_t>(m, uint8_t(m.bx()), 0x2)));
    m.bx() =
        uint16_t((m.bx() & 0xFF00) | uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx()) & 0x3))));
    m.set_al(uint8_t(m.rd8(m.cs(), uint16_t(m.bx() + 0x8709))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | 0x40))));
    m.wr8(m.ds(), uint16_t(m.si()), uint8_t(m.al()));
    goto branch_8700;
branch_869a:;
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldEventArgument))));
    m.set_ah(uint8_t(m.al()));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0xF))));
    if (m.flags() & emu::ZF)
        goto branch_86df;
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() ^ m.bx())));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.ah()));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::rotate_left<uint8_t>(m, uint8_t(m.bx()), 0x2)));
    m.bx() =
        uint16_t((m.bx() & 0xFF00) | uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx()) & 0x3))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.cs(), uint16_t(m.bx() + 0x8705))));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kBlinkEnabled))));
    computed::push(m, m.ax());
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(kBlinkEnabled), uint8_t(m.al()));
branch_86c0:;
    computed::push(m, m.bx());
    computed::push(m, m.cx());
    attempt_player_step_core(m);
    finish_player_move_core(m);
    m.cx() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    if (!(m.flags() & emu::ZF))
        goto branch_86c0;
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(kAnimationStep), uint8_t(m.al()));
    m.ax() = uint16_t(computed::pop(m));
    m.wr8(m.ss(), uint16_t(kBlinkEnabled), uint8_t(m.al()));
    goto branch_8700;
branch_86df:;
    m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() ^ m.bx())));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.ah()));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::rotate_left<uint8_t>(m, uint8_t(m.bx()), 0x2)));
    m.bx() =
        uint16_t((m.bx() & 0xFF00) | uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx()) & 0x3))));
    m.set_al(uint8_t(m.rd8(m.cs(), uint16_t(m.bx() + 0x8705))));
    m.wr8(m.ss(), uint16_t(kPlayerFacing), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.cs(), uint16_t(m.bx() + 0x8709))));
    m.wr8(m.ss(), uint16_t(kPlayerSpriteFacing), uint8_t(m.al()));
    update_field_frame_core(m);
branch_8700:;
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void event_move_or_turn_character(Machine &m) {
    event_move_or_turn_character_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x35870D (near)
// Dispatch field flashing, shaking, NPC clearing, instant text or an indefinite frame loop.
void event_apply_screen_effect_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldEventArgument))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() | m.al()))));
    if (m.flags() & emu::ZF)
        goto branch_873e;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    if (m.flags() & emu::ZF)
        goto branch_8772;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    if (m.flags() & emu::ZF)
        goto branch_873d;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    if (!(m.flags() & emu::ZF))
        goto branch_8724;
    goto branch_87cd;
branch_8724:;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    if (!(m.flags() & emu::ZF))
        goto branch_872b;
    goto branch_87e6;
branch_872b:;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    if (!(m.flags() & emu::ZF))
        goto branch_8732;
    goto branch_87e9;
branch_8732:;
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    if (!(m.flags() & emu::ZF))
        goto branch_873d;
branch_8736:;
    update_field_frame_core(m);
    goto branch_8736;
branch_873d:;
    return;
branch_873e:;
    // A field effect after the dialogue window closed: the native picture is complete, so a held host picture
    // releases and the effect shows.
    hooks::emit(hooks::Event::PresentationPicture, m, {2});
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.ax() = uint16_t(uint16_t(m.in8(uint8_t(0x0)) | (uint16_t(m.in8(uint8_t(0x0 + 1))) << 8)));
    computed::push(m, m.ax());
    m.set_ah(uint8_t(0xF2));
    computed::out_word(m, uint8_t(0x0), m.ax());
    m.cx() = uint16_t(0x4);
branch_874c:;
    computed::push(m, m.cx());
    computed::push(m, m.ax());
    m.cx() = uint16_t(0x4);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(0x0));
    computed::out_word(m, uint8_t(0x0), m.ax());
    m.cx() = uint16_t(0x4);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(computed::pop(m));
    computed::out_word(m, uint8_t(0x0), m.ax());
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_874c;
    m.ax() = uint16_t(computed::pop(m));
    computed::out_word(m, uint8_t(0x0), m.ax());
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    goto branch_873d;
branch_8772:;
    // A field effect after the dialogue window closed: the native picture is complete, so a held host picture
    // releases and the effect shows.
    hooks::emit(hooks::Event::PresentationPicture, m, {2});
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    draw_map_top_edge_core(m);
    draw_map_bottom_edge_call_core(m);
    draw_map_left_edge_call_core(m);
    draw_map_right_edge_core(m);
    m.ax() = uint16_t(uint16_t(m.in8(uint8_t(0x10)) | (uint16_t(m.in8(uint8_t(0x10 + 1))) << 8)));
    m.bx() = uint16_t(m.ax());
    m.cx() = uint16_t(0x1E);
branch_8791:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x1000);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x8)));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.al()));
    m.ax() = uint16_t(0x1000);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), 0x8)));
    m.dx() = uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.cx() = uint16_t(0x2);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.ax() = uint16_t(m.bx());
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), uint8_t(m.dx()))));
    m.set_ah(uint8_t(computed::add<uint8_t>(m, m.ah(), uint8_t(m.dx() >> 8))));
    computed::out_word(m, uint8_t(0x10), m.ax());
    computed::out_word(m, uint8_t(0x12), m.ax());
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_8791;
    m.ax() = uint16_t(m.bx());
    computed::out_word(m, uint8_t(0x10), m.ax());
    computed::out_word(m, uint8_t(0x12), m.ax());
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    goto branch_873d;
branch_87cd:;
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(0xFF));
    m.si() = uint16_t(kNpcTable);
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x14));
    m.wr8(m.ss(), uint16_t(m.si()), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    goto branch_873d;
branch_87e6:;
    goto branch_873d;
branch_87e9:;
    m.set_al(uint8_t(0xA));
    m.wr8(m.ss(), uint16_t(kDialogueSpeed), uint8_t(m.al()));
    goto branch_873d;
}
void event_apply_screen_effect(Machine &m) {
    event_apply_screen_effect_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x3587F2 (near)
// Dispatch the event parameter's high nibble through the extended command table.
void event_dispatch_extended_command_core(Machine &m) {
    m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() ^ m.bx())));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kFieldEventKind))));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldEventArgument))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::shift_right<uint8_t>(m, uint8_t(m.bx()), 0x3)));
    m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() & 0x1E)));
    m.bx() = uint16_t(m.rd16(m.cs(), uint16_t(m.bx() + 0x8812)));
    m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() | m.bx())));
    if (m.flags() & emu::ZF)
        goto branch_8811;
    {
        const uint16_t callback_cs = m.cs();
        dispatch_computed_handler_core(m, m.bx());
        if (m.cs() != callback_cs)
            return;
    }
branch_8811:;
    return;
}
void event_dispatch_extended_command(Machine &m) {
    event_dispatch_extended_command_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x358832 (near)
// Reset the movement timer before performing a timed palette darkening effect.
void event_freeze_and_darken_field_core(Machine &m) {
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr16(m.ss(), uint16_t(kMoveDelta), uint16_t(m.ax()));
    m.set_al(uint8_t(0x1));
    m.wr8(m.ss(), uint16_t(kAnimationStep), uint8_t(m.al()));
    event_darken_field_palettes_core(m);
    return;
}
void event_freeze_and_darken_field(Machine &m) {
    event_freeze_and_darken_field_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x35883E (near)
// Darken fifteen palettes for the encoded number of steps and inter-step delay.
void event_darken_field_palettes_core(Machine &m) {
    m.dx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.dx() ^ m.dx())));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.al()));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(computed::shift_right<uint8_t>(m, uint8_t(m.dx()), 0x4)));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.cx() =
        uint16_t((m.cx() & 0xFF00) | uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.cx()) & 0xF))));
branch_884a:;
    computed::push(m, m.cx());
    m.ax() = uint16_t(0x0);
    m.cx() = uint16_t(0xF);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        darken_palette_step_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    update_field_frame_core(m);
    m.cx() = uint16_t(m.dx());
    m.cx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.cx() | m.cx())));
    if (m.flags() & emu::ZF)
        goto branch_8866;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
branch_8866:;
    m.cx() = uint16_t(computed::pop(m));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(computed::increment<uint8_t>(m, uint8_t(m.cx()), true)));
    if (!(m.flags() & emu::ZF))
        goto branch_884a;
    return;
}
void event_darken_field_palettes(Machine &m) {
    event_darken_field_palettes_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x35886C (near)
// Play AL's immediate sound effect.
void event_play_immediate_sound_core(Machine &m) {
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        play_sound_effect_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void event_play_immediate_sound(Machine &m) {
    event_play_immediate_sound_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x358872 (near)
// Store AL as the next music request.
void event_set_music_request_core(Machine &m) {
    m.wr8(m.ss(), uint16_t(kMusicRequest), uint8_t(m.al()));
    return;
}
void event_set_music_request(Machine &m) {
    event_set_music_request_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x358877 (near)
// Store AL as the next queued sound effect.
void event_set_sound_request_core(Machine &m) {
    m.wr8(m.ss(), uint16_t(kSfxRequest), uint8_t(m.al()));
    return;
}
void event_set_sound_request(Machine &m) {
    event_set_sound_request_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x35887C (near)
// Dispatch AL through the intro command interpreter.
void event_run_intro_command_core(Machine &m) {
    const uint16_t caller_cs = m.cs();
    if (m.al() >= 73) {
        // The malformed-opcode handoff also executes the original RETF/RET epilogues. Supply the
        // real return frame of this LCALL, which valid native core calls normally omit.
        computed::push(m, caller_cs);
        computed::push(m, 0x8881);
    }
    m.cs() = 0xC000;
    if (!dispatch_intro_command_core(m))
        return;
    m.cs() = caller_cs;
}
void event_run_intro_command(Machine &m) {
    event_run_intro_command_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x358882 (near)
// Start shared sound glide mode one with AL's packed parameters.
void event_start_upward_portamento_core(Machine &m) {
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.cx() =
        uint16_t((m.cx() & 0xFF00) | uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.cx()) & 0xF))));
    m.cx() = uint16_t((m.cx() & 0x00FF) |
                      (uint16_t(uint8_t(computed::shift_right<uint8_t>(m, uint8_t(m.cx() >> 8), 0x4))) << 8));
    m.ax() = uint16_t(0x1);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        sound_channel_reset_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void event_start_upward_portamento(Machine &m) {
    event_start_upward_portamento_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x358895 (near)
// Start shared sound glide mode two with AL's packed parameters.
void event_start_downward_portamento_core(Machine &m) {
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.al()));
    m.cx() = uint16_t((m.cx() & 0x00FF) | (uint16_t(uint8_t(m.al())) << 8));
    m.cx() =
        uint16_t((m.cx() & 0xFF00) | uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.cx()) & 0xF))));
    m.cx() = uint16_t((m.cx() & 0x00FF) |
                      (uint16_t(uint8_t(computed::shift_right<uint8_t>(m, uint8_t(m.cx() >> 8), 0x4))) << 8));
    m.ax() = uint16_t(0x2);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0xd000;
        sound_channel_reset_entry_core(m);
        if (m.cs() != 0xd000)
            return;
        m.cs() = caller_cs;
    }
    return;
}
void event_start_downward_portamento(Machine &m) {
    event_start_downward_portamento_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x3588A8 (near)
// Store map 128, cell 48:10 and facing zero in the second parked vehicle record.
void event_place_vehicle_at_fixed_destination_core(Machine &m) {
    m.set_al(uint8_t(0x80));
    m.wr8(m.ss(), uint16_t(kSecondVehicleMap), uint8_t(m.al()));
    m.set_al(uint8_t(0x30));
    m.wr8(m.ss(), uint16_t(kSecondVehicleX), uint8_t(m.al()));
    m.set_al(uint8_t(0xA));
    m.wr8(m.ss(), uint16_t(kSecondVehicleY), uint8_t(m.al()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.wr8(m.ss(), uint16_t(kSecondVehicleState), uint8_t(m.al()));
    return;
}
void event_place_vehicle_at_fixed_destination(Machine &m) {
    event_place_vehicle_at_fixed_destination_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x3588C1 (near)
// Store AL's low five bits as the player animation.
void event_set_player_animation_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x1F))));
    m.wr8(m.ss(), uint16_t(kPlayerSpriteFacing), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void event_set_player_animation(Machine &m) {
    event_set_player_animation_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x3588D0 (near)
// Show a facing-dependent player gesture for thirty field frames.
void event_animate_player_gesture_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kPlayerSpriteFacing))));
    computed::push(m, m.ax());
    m.bx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.bx() ^ m.bx())));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(kPlayerFacing))));
    m.set_al(uint8_t(m.rd8(m.cs(), uint16_t(m.bx() + 0x88FE))));
    m.wr8(m.ss(), uint16_t(kPlayerSpriteFacing), uint8_t(m.al()));
    m.cx() = uint16_t(0x1E);
branch_88ec:;
    update_field_frame_core(m);
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_88ec;
    m.ax() = uint16_t(computed::pop(m));
    m.wr8(m.ss(), uint16_t(kPlayerSpriteFacing), uint8_t(m.al()));
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void event_animate_player_gesture(Machine &m) {
    event_animate_player_gesture_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x358902 (near)
// Change the selected NPC's sprite from AL's packed index and sprite ID.
void event_set_npc_sprite_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.set_al(uint8_t(computed::rotate_left<uint8_t>(m, m.al(), 0x3)));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x7))));
    m.set_ah(uint8_t(0x14));
    {
        const uint16_t product = uint16_t(m.al()) * m.ah();
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.bp() = uint16_t(m.ax());
    m.bp() = uint16_t(computed::add<uint16_t>(m, m.bp(), kNpcTable));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si()))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xFF);
    if (m.flags() & emu::ZF)
        goto branch_8924;
    m.bx() =
        uint16_t((m.bx() & 0xFF00) | uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx()) & 0x1F))));
    m.wr8(m.ss(), uint16_t(m.bp()), uint8_t(uint8_t(m.bx())));
branch_8924:;
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void event_set_npc_sprite(Machine &m) {
    event_set_npc_sprite_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x358929 (near)
// Force a player step while preserving the event interpreter's registers.
void event_force_player_step_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    force_player_step_core(m);
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void event_force_player_step(Machine &m) {
    event_force_player_step_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x358937 (near)
// Move the player forward while preserving the event interpreter's registers.
void event_move_player_forward_core(Machine &m) {
    computed::save_registers(m);
    computed::push(m, m.ds());
    computed::push(m, m.es());
    computed::push(m, m.ss());
    // Reproduce the ROM's mismatched LCALL 5000:0830 / near RET. The return IP is consumed
    // by 0830, but its CS word remains for the following POP SS; this opcode corrupts its frame.
    computed::push(m, m.cs());
    {
        const uint16_t callback_cs = m.cs();
        move_player_forward_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ss() = uint16_t(computed::pop(m));
    m.es() = uint16_t(computed::pop(m));
    m.ds() = uint16_t(computed::pop(m));
    computed::restore_registers(m);
    return;
}
void event_move_player_forward(Machine &m) {
    event_move_player_forward_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x3595DB (near)
// Upload three four-frame strips and one half-rate two-frame strip every eight ticks.
void animate_field_mixed_four_frame_tiles_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationTimer))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    if (m.flags() & emu::ZF)
        goto branch_95ea;
    goto branch_9678;
branch_95ea:;
    m.set_al(uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationFrame),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame)))));
    m.set_al(uint8_t(0xEB));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.ss());
    m.es() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x3))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x0));
    m.di() = uint16_t(0x8000);
    m.cx() = uint16_t(0x3);
branch_9619:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x200));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_9619;
    m.set_al(uint8_t(0xED));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::shift_right<uint8_t>(m, m.al(), 0x1)));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x1))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x0));
    m.di() = uint16_t(0x8180);
    m.cx() = uint16_t(0x1);
branch_9657:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x100));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_9657;
branch_9678:;
    return;
}
void animate_field_mixed_four_frame_tiles(Machine &m) {
    animate_field_mixed_four_frame_tiles_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x359679 (near)
// Upload two half-rate two-frame tile strips every ten ticks.
void animate_field_half_rate_two_frame_tiles_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationTimer))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    if (!(m.flags() & emu::ZF))
        goto branch_96d7;
    m.set_al(uint8_t(0xA));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationFrame),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame)))));
    m.set_al(uint8_t(0xED));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.ss());
    m.es() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::shift_right<uint8_t>(m, m.al(), 0x1)));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x1))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x100));
    m.di() = uint16_t(0x8000);
    m.cx() = uint16_t(0x2);
branch_96b6:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x100));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_96b6;
branch_96d7:;
    return;
}
void animate_field_half_rate_two_frame_tiles(Machine &m) {
    animate_field_half_rate_two_frame_tiles_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x3596D8 (near)
// Upload nine two-frame strips and three half-rate strips every eight ticks.
void animate_field_nine_strip_two_frame_tiles_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationTimer))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    if (m.flags() & emu::ZF)
        goto branch_96e7;
    goto branch_9775;
branch_96e7:;
    m.set_al(uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationFrame),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame)))));
    m.set_al(uint8_t(0xEC));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.ss());
    m.es() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x1))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), kNineStripTwoFrameTiles));
    m.di() = uint16_t(0x8000);
    m.cx() = uint16_t(0x9);
branch_9716:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x100));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_9716;
    m.set_al(uint8_t(0xED));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::shift_right<uint8_t>(m, m.al(), 0x1)));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x1))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x300));
    m.di() = uint16_t(0x8480);
    m.cx() = uint16_t(0x3);
branch_9754:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x100));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_9754;
branch_9775:;
    return;
}
void animate_field_nine_strip_two_frame_tiles(Machine &m) {
    animate_field_nine_strip_two_frame_tiles_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x359776 (near)
// Upload five eight-frame strips, six two-frame strips and one half-rate strip.
void animate_field_mixed_eight_frame_tiles_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationTimer))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    if (m.flags() & emu::ZF)
        goto branch_9785;
    goto branch_984a;
branch_9785:;
    m.set_al(uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationFrame),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame)))));
    m.set_al(uint8_t(0xEB));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.ss());
    m.es() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x7))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x600));
    m.di() = uint16_t(0x8000);
    m.cx() = uint16_t(0x5);
branch_97b4:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x400));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_97b4;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x1))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x28)));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x600));
    m.cx() = uint16_t(0x6);
branch_97eb:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x100));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_97eb;
    m.set_al(uint8_t(0xED));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::shift_right<uint8_t>(m, m.al(), 0x1)));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x1))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x600));
    m.di() = uint16_t(0x8580);
    m.cx() = uint16_t(0x1);
branch_9829:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x100));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_9829;
branch_984a:;
    return;
}
void animate_field_mixed_eight_frame_tiles(Machine &m) {
    animate_field_mixed_eight_frame_tiles_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x35984B (near)
// Upload nine four-frame tile strips every eight ticks.
void animate_field_nine_strip_four_frame_tiles_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationTimer))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    if (!(m.flags() & emu::ZF))
        goto branch_98a7;
    m.set_al(uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationFrame),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame)))));
    m.set_al(uint8_t(0xEB));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.ss());
    m.es() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x3))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2E00));
    m.di() = uint16_t(0x8000);
    m.cx() = uint16_t(0x9);
branch_9886:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x200));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_9886;
branch_98a7:;
    return;
}
void animate_field_nine_strip_four_frame_tiles(Machine &m) {
    animate_field_nine_strip_four_frame_tiles_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x3598A8 (near)
// Upload three eight-frame tile strips every eight ticks.
void animate_field_three_strip_eight_frame_tiles_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationTimer))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    if (!(m.flags() & emu::ZF))
        goto branch_9904;
    m.set_al(uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationFrame),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame)))));
    m.set_al(uint8_t(0xEB));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.ss());
    m.es() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x7))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x4000));
    m.di() = uint16_t(0x8000);
    m.cx() = uint16_t(0x3);
branch_98e3:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x400));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_98e3;
branch_9904:;
    return;
}
void animate_field_three_strip_eight_frame_tiles(Machine &m) {
    animate_field_three_strip_eight_frame_tiles_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x359905 (near)
// Upload two eight-frame tile strips from the first two-strip asset set.
void animate_field_two_strip_eight_frame_tiles_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationTimer))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    if (!(m.flags() & emu::ZF))
        goto branch_9961;
    m.set_al(uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationFrame),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame)))));
    m.set_al(uint8_t(0xEB));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.ss());
    m.es() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x7))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x5800));
    m.di() = uint16_t(0x8000);
    m.cx() = uint16_t(0x2);
branch_9940:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x400));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_9940;
branch_9961:;
    return;
}
void animate_field_two_strip_eight_frame_tiles(Machine &m) {
    animate_field_two_strip_eight_frame_tiles_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x359962 (near)
// Upload four eight-frame tile strips every eight ticks.
void animate_field_four_strip_eight_frame_tiles_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationTimer))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    if (!(m.flags() & emu::ZF))
        goto branch_99be;
    m.set_al(uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationFrame),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame)))));
    m.set_al(uint8_t(0xEB));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.ss());
    m.es() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x7))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x6800));
    m.di() = uint16_t(0x8000);
    m.cx() = uint16_t(0x4);
branch_999d:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x400));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_999d;
branch_99be:;
    return;
}
void animate_field_four_strip_eight_frame_tiles(Machine &m) {
    animate_field_four_strip_eight_frame_tiles_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x3599BF (near)
// Upload two eight-frame tile strips from the alternate two-strip asset set.
void animate_field_alternate_two_strip_tiles_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationTimer))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    if (!(m.flags() & emu::ZF))
        goto branch_9a1b;
    m.set_al(uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationFrame),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame)))));
    m.set_al(uint8_t(0xEB));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.ss());
    m.es() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x7))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x8800));
    m.di() = uint16_t(0x8000);
    m.cx() = uint16_t(0x2);
branch_99fa:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x400));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_99fa;
branch_9a1b:;
    return;
}
void animate_field_alternate_two_strip_tiles(Machine &m) {
    animate_field_alternate_two_strip_tiles_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x359A1C (near)
// Upload five eight-frame tile strips every eight ticks.
void animate_field_five_strip_eight_frame_tiles_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationTimer))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    if (!(m.flags() & emu::ZF))
        goto branch_9a78;
    m.set_al(uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationFrame),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame)))));
    m.set_al(uint8_t(0xCB));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.ss());
    m.es() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x7))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x0));
    m.di() = uint16_t(0x8000);
    m.cx() = uint16_t(0x5);
branch_9a57:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x400));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_9a57;
branch_9a78:;
    return;
}
void animate_field_five_strip_eight_frame_tiles(Machine &m) {
    animate_field_five_strip_eight_frame_tiles_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x359A79 (near)
// Upload three sixteen-frame tile strips every six ticks.
void animate_field_three_strip_sixteen_frame_tiles_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationTimer))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    if (!(m.flags() & emu::ZF))
        goto branch_9ad5;
    m.set_al(uint8_t(0x6));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationFrame),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame)))));
    m.set_al(uint8_t(0xCB));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.ss());
    m.es() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0xF))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2800));
    m.di() = uint16_t(0x8000);
    m.cx() = uint16_t(0x3);
branch_9ab4:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x800));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_9ab4;
branch_9ad5:;
    return;
}
void animate_field_three_strip_sixteen_frame_tiles(Machine &m) {
    animate_field_three_strip_sixteen_frame_tiles_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x359AD6 (near)
// Upload six two-frame tile strips every eight ticks.
void animate_field_six_strip_two_frame_tiles_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationTimer))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    if (!(m.flags() & emu::ZF))
        goto branch_9b32;
    m.set_al(uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationFrame),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame)))));
    m.set_al(uint8_t(0xED));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.ss());
    m.es() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x1))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x6D00));
    m.di() = uint16_t(0x8000);
    m.cx() = uint16_t(0x6);
branch_9b11:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x100));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_9b11;
branch_9b32:;
    return;
}
void animate_field_six_strip_two_frame_tiles(Machine &m) {
    animate_field_six_strip_two_frame_tiles_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x359B33 (near)
// Upload three sixteen-frame tile strips every four ticks.
void animate_field_fast_sixteen_frame_tiles_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationTimer))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    if (!(m.flags() & emu::ZF))
        goto branch_9b8f;
    m.set_al(uint8_t(0x4));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationFrame),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame)))));
    m.set_al(uint8_t(0xED));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.ss());
    m.es() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0xF))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x1500));
    m.di() = uint16_t(0x8000);
    m.cx() = uint16_t(0x3);
branch_9b6e:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x800));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_9b6e;
branch_9b8f:;
    return;
}
void animate_field_fast_sixteen_frame_tiles(Machine &m) {
    animate_field_fast_sixteen_frame_tiles_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x359B90 (near)
// Upload two four-frame strips from the primary fast asset set every four ticks.
void animate_field_fast_two_strip_four_frame_tiles_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationTimer))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    if (!(m.flags() & emu::ZF))
        goto branch_9bec;
    m.set_al(uint8_t(0x4));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationFrame),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame)))));
    m.set_al(uint8_t(0xEC));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.ss());
    m.es() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x3))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), kFastTwoStripFourFrameTiles));
    m.di() = uint16_t(0x8000);
    m.cx() = uint16_t(0x2);
branch_9bcb:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x200));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_9bcb;
branch_9bec:;
    return;
}
void animate_field_fast_two_strip_four_frame_tiles(Machine &m) {
    animate_field_fast_two_strip_four_frame_tiles_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x359BED (near)
// Upload two four-frame strips from the alternate fast asset set every four ticks.
void animate_field_alternate_fast_four_frame_tiles_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationTimer))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    if (!(m.flags() & emu::ZF))
        goto branch_9c49;
    m.set_al(uint8_t(0x4));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationFrame),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame)))));
    m.set_al(uint8_t(0xCB));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.ss());
    m.es() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x3))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x4000));
    m.di() = uint16_t(0x8000);
    m.cx() = uint16_t(0x2);
branch_9c28:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x200));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_9c28;
branch_9c49:;
    return;
}
void animate_field_alternate_fast_four_frame_tiles(Machine &m) {
    animate_field_alternate_fast_four_frame_tiles_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x359C4A (near)
// Upload one four-frame tile strip every six ticks with an explicitly wrapped frame index.
void animate_field_single_four_frame_tile_strip_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationTimer))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    if (!(m.flags() & emu::ZF))
        goto branch_9cb1;
    m.set_al(uint8_t(0x6));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_9c6a;
    m.set_al(uint8_t(0x0));
branch_9c6a:;
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationFrame), uint8_t(m.al()));
    m.set_al(uint8_t(0xCC));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.ss());
    m.es() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x0));
    m.di() = uint16_t(0x8000);
    m.cx() = uint16_t(0x1);
branch_9c90:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x200));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_9c90;
branch_9cb1:;
    return;
}
void animate_field_single_four_frame_tile_strip(Machine &m) {
    animate_field_single_four_frame_tile_strip_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x359CB2 (near)
// Upload mixed-rate tile strips, selecting alternate assets for map 122 at progress stage fourteen.
void animate_field_progress_dependent_tiles_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationTimer))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    if (m.flags() & emu::ZF)
        goto branch_9cc1;
    goto branch_9dfb;
branch_9cc1:;
    m.set_al(uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationFrame),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame)))));
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.ss());
    m.es() = uint16_t(m.ax());
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kMapId))));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x7A);
    if (!(m.flags() & emu::ZF))
        goto branch_9d55;
    m.set_al(uint8_t(0x6));
    read_packed_progress_core(m);
    (void)computed::subtract<uint8_t>(m, m.al(), 0xE);
    if (!(m.flags() & emu::ZF))
        goto branch_9d55;
    m.set_al(uint8_t(0xEC));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0xF))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x4000));
    m.di() = uint16_t(0x8000);
    m.cx() = uint16_t(0x4);
branch_9d03:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x800));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_9d03;
    m.set_al(uint8_t(0xEC));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x1F))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x0));
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    goto branch_9d8d;
branch_9d55:;
    m.set_al(uint8_t(0xEC));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x1F))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x0));
    m.di() = uint16_t(0x8000);
    m.cx() = uint16_t(0x5);
branch_9d70:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_9d70;
branch_9d8d:;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x3))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x20)));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x0));
    m.cx() = uint16_t(0xC);
branch_9da3:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x200));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_9da3;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x1))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x50)));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x0));
    m.cx() = uint16_t(0x11);
branch_9dda:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x100));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_9dda;
branch_9dfb:;
    return;
}
void animate_field_progress_dependent_tiles(Machine &m) {
    animate_field_progress_dependent_tiles_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x359DFC (near)
// Upload three sixteen-frame strips and seven eight-frame strips every four ticks.
void animate_field_mixed_sixteen_frame_tiles_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationTimer))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    if (m.flags() & emu::ZF)
        goto branch_9e0b;
    goto branch_9e92;
branch_9e0b:;
    m.set_al(uint8_t(0x4));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationFrame),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame)))));
    m.set_al(uint8_t(0xED));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.ss());
    m.es() = uint16_t(m.ax());
    m.di() = uint16_t(0x8000);
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0xF))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2D00));
    m.cx() = uint16_t(0x3);
branch_9e3a:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x800));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_9e3a;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x7))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x30)));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x2D00));
    m.cx() = uint16_t(0x7);
branch_9e71:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x400));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_9e71;
branch_9e92:;
    return;
}
void animate_field_mixed_sixteen_frame_tiles(Machine &m) {
    animate_field_mixed_sixteen_frame_tiles_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x359E93 (near)
// Upload one four-frame tile strip every four ticks.
void animate_field_fast_single_four_frame_strip_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationTimer))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    if (!(m.flags() & emu::ZF))
        goto branch_9eef;
    m.set_al(uint8_t(0x4));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationFrame),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame)))));
    m.set_al(uint8_t(0xEC));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.ss());
    m.es() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x3))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), kFastSingleFourFrameTiles));
    m.di() = uint16_t(0x8000);
    m.cx() = uint16_t(0x1);
branch_9ece:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x200));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_9ece;
branch_9eef:;
    return;
}
void animate_field_fast_single_four_frame_strip(Machine &m) {
    animate_field_fast_single_four_frame_strip_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x359EF0 (near)
// Upload one thirty-two-frame tile strip every six ticks.
void animate_field_single_thirty_two_frame_strip_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationTimer))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    if (!(m.flags() & emu::ZF))
        goto branch_9f4c;
    m.set_al(uint8_t(0x6));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationFrame),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame)))));
    m.set_al(uint8_t(0xEB));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.ss());
    m.es() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x1F))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x9800));
    m.di() = uint16_t(0x8000);
    m.cx() = uint16_t(0x1);
branch_9f2b:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x1000));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_9f2b;
branch_9f4c:;
    return;
}
void animate_field_single_thirty_two_frame_strip(Machine &m) {
    animate_field_single_thirty_two_frame_strip_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x359F4D (near)
// Upload tile groups with sixteen-, four-, sixteen- and eight-frame cycles.
void animate_field_four_rate_tile_groups_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationTimer))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    if (m.flags() & emu::ZF)
        goto branch_9f5c;
    goto branch_a051;
branch_9f5c:;
    m.set_al(uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationFrame),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame)))));
    m.set_al(uint8_t(0xEC));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.ss());
    m.es() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0xF))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x6800));
    m.di() = uint16_t(0x8000);
    m.cx() = uint16_t(0x1);
branch_9f8b:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x800));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_9f8b;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x3))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x10)));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x6800));
    m.cx() = uint16_t(0x7);
branch_9fc2:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x200));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_9fc2;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0xF))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x2C)));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x6800));
    m.cx() = uint16_t(0x4);
branch_9ff9:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x800));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_9ff9;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x7))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x6C)));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x6800));
    m.cx() = uint16_t(0x2);
branch_a030:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x400));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_a030;
branch_a051:;
    return;
}
void animate_field_four_rate_tile_groups(Machine &m) {
    animate_field_four_rate_tile_groups_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x35A052 (near)
// Upload four sixteen-frame strips and two two-frame strips every six ticks.
void animate_field_six_tick_mixed_tiles_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationTimer))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    if (m.flags() & emu::ZF)
        goto branch_a061;
    goto branch_a0e8;
branch_a061:;
    m.set_al(uint8_t(0x6));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationFrame),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame)))));
    m.set_al(uint8_t(0xEC));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.ss());
    m.es() = uint16_t(m.ax());
    m.di() = uint16_t(0x8000);
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0xF))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xA800));
    m.cx() = uint16_t(0x4);
branch_a090:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x800));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_a090;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x1))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x40)));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xA800));
    m.cx() = uint16_t(0x2);
branch_a0c7:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x100));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_a0c7;
branch_a0e8:;
    return;
}
void animate_field_six_tick_mixed_tiles(Machine &m) {
    animate_field_six_tick_mixed_tiles_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x35A0E9 (near)
// Upload six two-frame tile strips every twelve ticks.
void animate_field_slow_six_strip_tiles_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationTimer))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    if (!(m.flags() & emu::ZF))
        goto branch_a145;
    m.set_al(uint8_t(0xC));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationFrame),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame)))));
    m.set_al(uint8_t(0xED));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.ss());
    m.es() = uint16_t(m.ax());
    m.di() = uint16_t(0x8000);
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x1))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0xB300));
    m.cx() = uint16_t(0x6);
branch_a124:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x100));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_a124;
branch_a145:;
    return;
}
void animate_field_slow_six_strip_tiles(Machine &m) {
    animate_field_slow_six_strip_tiles_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x35A146 (near)
// Upload two two-frame strips and twenty-one half-rate four-frame strips.
void animate_field_half_rate_four_frame_tiles_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationTimer))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    if (m.flags() & emu::ZF)
        goto branch_a155;
    goto branch_a1de;
branch_a155:;
    m.set_al(uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationFrame),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame)))));
    m.set_al(uint8_t(0xED));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.ss());
    m.es() = uint16_t(m.ax());
    m.di() = uint16_t(0x8000);
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x1))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x7300));
    m.cx() = uint16_t(0x2);
branch_a184:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x100));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_a184;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::shift_right<uint8_t>(m, m.al(), 0x1)));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x3))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x4)));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x7300));
    m.cx() = uint16_t(0x15);
branch_a1bd:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x200));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_a1bd;
branch_a1de:;
    return;
}
void animate_field_half_rate_four_frame_tiles(Machine &m) {
    animate_field_half_rate_four_frame_tiles_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x35A1DF (near)
// Upload one four-frame strip and four two-frame strips every six ticks.
void animate_field_mixed_four_and_two_frame_tiles_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationTimer))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    if (m.flags() & emu::ZF)
        goto branch_a1ee;
    goto branch_a275;
branch_a1ee:;
    m.set_al(uint8_t(0x6));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationFrame),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame)))));
    m.set_al(uint8_t(0xEC));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.ss());
    m.es() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x3))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), kMixedFourAndTwoFrameTiles));
    m.di() = uint16_t(0x8000);
    m.cx() = uint16_t(0x1);
branch_a21d:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x200));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_a21d;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x1))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x4)));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), kMixedFourAndTwoFrameTiles));
    m.cx() = uint16_t(0x4);
branch_a254:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x100));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_a254;
branch_a275:;
    return;
}
void animate_field_mixed_four_and_two_frame_tiles(Machine &m) {
    animate_field_mixed_four_and_two_frame_tiles_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x35A276 (near)
// Upload four eight-frame tile strips from the alternate four-strip asset set.
void animate_field_alternate_four_strip_tiles_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationTimer))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    if (!(m.flags() & emu::ZF))
        goto branch_a2d2;
    m.set_al(uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationTimer), uint8_t(m.al()));
    m.wr8(m.ss(), uint16_t(kFieldTileAnimationFrame),
          uint8_t(computed::increment<uint8_t>(m, m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame)))));
    m.set_al(uint8_t(0xEC));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.ss());
    m.es() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldTileAnimationFrame))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0x7))));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x7));
    m.si() = uint16_t(m.ax());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), kAlternateFourStripTiles));
    m.di() = uint16_t(0x8000);
    m.cx() = uint16_t(0x4);
branch_a2b1:;
    m.ax() = uint16_t(m.si());
    computed::out_word(m, uint8_t(0x40), m.ax());
    m.ax() = uint16_t(0x2);
    computed::out_word(m, uint8_t(0x42), m.ax());
    m.ax() = uint16_t(m.di());
    computed::out_word(m, uint8_t(0x44), m.ax());
    m.ax() = uint16_t(0x80);
    computed::out_word(m, uint8_t(0x46), m.ax());
    m.set_al(uint8_t(0x80));
    m.out8(uint8_t(0x48), m.al());
    m.si() = uint16_t(computed::add<uint16_t>(m, m.si(), 0x400));
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x80));
    m.cx() = uint16_t(computed::increment<uint16_t>(m, m.cx(), true));
    if (!(m.flags() & emu::ZF))
        goto branch_a2b1;
branch_a2d2:;
    return;
}
void animate_field_alternate_four_strip_tiles(Machine &m) {
    animate_field_alternate_four_strip_tiles_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x35A4AA (near)
// Cycle palette slots one and three through two animation frames.
void animate_field_dual_two_frame_palettes_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldPaletteTimer))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ss(), uint16_t(kFieldPaletteTimer), uint8_t(m.al()));
    if (!(m.flags() & emu::ZF))
        goto branch_a51d;
    m.set_al(uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kFieldPaletteTimer), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldPaletteFirstFrame))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::CF)
        goto branch_a4c8;
    m.set_al(uint8_t(0x0));
branch_a4c8:;
    m.wr8(m.ss(), uint16_t(kFieldPaletteFirstFrame), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldPaletteSecondFrame))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::CF)
        goto branch_a4d8;
    m.set_al(uint8_t(0x0));
branch_a4d8:;
    m.wr8(m.ss(), uint16_t(kFieldPaletteSecondFrame), uint8_t(m.al()));
    m.set_al(uint8_t(0xCF));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.ss());
    m.es() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldPaletteFirstFrame))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x4)));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x5));
    m.ax() =
        uint16_t(computed::add<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kFieldAnimationPaletteSource))));
    m.si() = uint16_t(m.ax());
    m.di() = uint16_t((kPaletteBuffer + 0x20));
    m.cx() = uint16_t(0x10);
    computed::copy_words(m);
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldPaletteSecondFrame))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x6)));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x5));
    m.ax() =
        uint16_t(computed::add<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kFieldAnimationPaletteSource))));
    m.si() = uint16_t(m.ax());
    m.di() = uint16_t((kPaletteBuffer + 0x60));
    m.cx() = uint16_t(0x10);
    computed::copy_words(m);
branch_a51d:;
    return;
}
void animate_field_dual_two_frame_palettes(Machine &m) {
    animate_field_dual_two_frame_palettes_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x35A51E (near)
// Cycle palette slot one through six animation frames.
void animate_field_palette_one_six_frames_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldPaletteTimer))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ss(), uint16_t(kFieldPaletteTimer), uint8_t(m.al()));
    if (!(m.flags() & emu::ZF))
        goto branch_a577;
    m.set_al(uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kFieldPaletteTimer), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldPaletteFirstFrame))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6);
    if (m.flags() & emu::CF)
        goto branch_a53c;
    m.set_al(uint8_t(0x0));
branch_a53c:;
    m.wr8(m.ss(), uint16_t(kFieldPaletteFirstFrame), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldPaletteSecondFrame))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6);
    if (m.flags() & emu::CF)
        goto branch_a54c;
    m.set_al(uint8_t(0x0));
branch_a54c:;
    m.wr8(m.ss(), uint16_t(kFieldPaletteSecondFrame), uint8_t(m.al()));
    m.set_al(uint8_t(0xCF));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.ss());
    m.es() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldPaletteFirstFrame))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x4)));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x5));
    m.ax() =
        uint16_t(computed::add<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kFieldAnimationPaletteSource))));
    m.si() = uint16_t(m.ax());
    m.di() = uint16_t((kPaletteBuffer + 0x20));
    m.cx() = uint16_t(0x10);
    computed::copy_words(m);
branch_a577:;
    return;
}
void animate_field_palette_one_six_frames(Machine &m) {
    animate_field_palette_one_six_frames_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x35A578 (near)
// Cycle palette slot three through twelve animation frames.
void animate_field_palette_three_twelve_frames_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldPaletteTimer))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ss(), uint16_t(kFieldPaletteTimer), uint8_t(m.al()));
    if (!(m.flags() & emu::ZF))
        goto branch_a5d1;
    m.set_al(uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kFieldPaletteTimer), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldPaletteFirstFrame))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xC);
    if (m.flags() & emu::CF)
        goto branch_a596;
    m.set_al(uint8_t(0x0));
branch_a596:;
    m.wr8(m.ss(), uint16_t(kFieldPaletteFirstFrame), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldPaletteSecondFrame))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), 0xC);
    if (m.flags() & emu::CF)
        goto branch_a5a6;
    m.set_al(uint8_t(0x0));
branch_a5a6:;
    m.wr8(m.ss(), uint16_t(kFieldPaletteSecondFrame), uint8_t(m.al()));
    m.set_al(uint8_t(0xCF));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.ss());
    m.es() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldPaletteFirstFrame))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x4)));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x5));
    m.ax() =
        uint16_t(computed::add<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kFieldAnimationPaletteSource))));
    m.si() = uint16_t(m.ax());
    m.di() = uint16_t((kPaletteBuffer + 0x60));
    m.cx() = uint16_t(0x10);
    computed::copy_words(m);
branch_a5d1:;
    return;
}
void animate_field_palette_three_twelve_frames(Machine &m) {
    animate_field_palette_three_twelve_frames_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x35A5D2 (near)
// Cycle palette slot three through the secondary six-frame sequence.
void animate_field_palette_three_six_frames_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldPaletteTimer))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ss(), uint16_t(kFieldPaletteTimer), uint8_t(m.al()));
    if (!(m.flags() & emu::ZF))
        goto branch_a62b;
    m.set_al(uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kFieldPaletteTimer), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldPaletteFirstFrame))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6);
    if (m.flags() & emu::CF)
        goto branch_a5f0;
    m.set_al(uint8_t(0x0));
branch_a5f0:;
    m.wr8(m.ss(), uint16_t(kFieldPaletteFirstFrame), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldPaletteSecondFrame))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6);
    if (m.flags() & emu::CF)
        goto branch_a600;
    m.set_al(uint8_t(0x0));
branch_a600:;
    m.wr8(m.ss(), uint16_t(kFieldPaletteSecondFrame), uint8_t(m.al()));
    m.set_al(uint8_t(0xCF));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.ss());
    m.es() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldPaletteSecondFrame))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0xA)));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x5));
    m.ax() =
        uint16_t(computed::add<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kFieldAnimationPaletteSource))));
    m.si() = uint16_t(m.ax());
    m.di() = uint16_t((kPaletteBuffer + 0x60));
    m.cx() = uint16_t(0x10);
    computed::copy_words(m);
branch_a62b:;
    return;
}
void animate_field_palette_three_six_frames(Machine &m) {
    animate_field_palette_three_six_frames_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x35A62C (near)
// Cycle palette slot two through six animation frames.
void animate_field_palette_two_six_frames_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldPaletteTimer))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ss(), uint16_t(kFieldPaletteTimer), uint8_t(m.al()));
    if (!(m.flags() & emu::ZF))
        goto branch_a685;
    m.set_al(uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kFieldPaletteTimer), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldPaletteFirstFrame))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6);
    if (m.flags() & emu::CF)
        goto branch_a64a;
    m.set_al(uint8_t(0x0));
branch_a64a:;
    m.wr8(m.ss(), uint16_t(kFieldPaletteFirstFrame), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldPaletteSecondFrame))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6);
    if (m.flags() & emu::CF)
        goto branch_a65a;
    m.set_al(uint8_t(0x0));
branch_a65a:;
    m.wr8(m.ss(), uint16_t(kFieldPaletteSecondFrame), uint8_t(m.al()));
    m.set_al(uint8_t(0xCF));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.ss());
    m.es() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldPaletteFirstFrame))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x4)));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x5));
    m.ax() =
        uint16_t(computed::add<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kFieldAnimationPaletteSource))));
    m.si() = uint16_t(m.ax());
    m.di() = uint16_t((kPaletteBuffer + 0x40));
    m.cx() = uint16_t(0x10);
    computed::copy_words(m);
branch_a685:;
    return;
}
void animate_field_palette_two_six_frames(Machine &m) {
    animate_field_palette_two_six_frames_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x35A6FA (near)
// Cycle palette slot two through two frames and slot three through six frames.
void animate_field_mixed_two_and_six_frame_palettes_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldPaletteTimer))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ss(), uint16_t(kFieldPaletteTimer), uint8_t(m.al()));
    if (!(m.flags() & emu::ZF))
        goto branch_a76d;
    m.set_al(uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kFieldPaletteTimer), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldPaletteFirstFrame))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::CF)
        goto branch_a718;
    m.set_al(uint8_t(0x0));
branch_a718:;
    m.wr8(m.ss(), uint16_t(kFieldPaletteFirstFrame), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldPaletteSecondFrame))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x6);
    if (m.flags() & emu::CF)
        goto branch_a728;
    m.set_al(uint8_t(0x0));
branch_a728:;
    m.wr8(m.ss(), uint16_t(kFieldPaletteSecondFrame), uint8_t(m.al()));
    m.set_al(uint8_t(0xCF));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.ss());
    m.es() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldPaletteFirstFrame))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x4)));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x5));
    m.ax() =
        uint16_t(computed::add<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kFieldAnimationPaletteSource))));
    m.si() = uint16_t(m.ax());
    m.di() = uint16_t((kPaletteBuffer + 0x40));
    m.cx() = uint16_t(0x10);
    computed::copy_words(m);
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldPaletteSecondFrame))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0xA)));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x5));
    m.ax() =
        uint16_t(computed::add<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kFieldAnimationPaletteSource))));
    m.si() = uint16_t(m.ax());
    m.di() = uint16_t((kPaletteBuffer + 0x60));
    m.cx() = uint16_t(0x10);
    computed::copy_words(m);
branch_a76d:;
    return;
}
void animate_field_mixed_two_and_six_frame_palettes(Machine &m) {
    animate_field_mixed_two_and_six_frame_palettes_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x35A76E (near)
// Cycle palette slot three through four animation frames.
void animate_field_palette_three_four_frames_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldPaletteTimer))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ss(), uint16_t(kFieldPaletteTimer), uint8_t(m.al()));
    if (!(m.flags() & emu::ZF))
        goto branch_a7c7;
    m.set_al(uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kFieldPaletteTimer), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldPaletteFirstFrame))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_a78c;
    m.set_al(uint8_t(0x0));
branch_a78c:;
    m.wr8(m.ss(), uint16_t(kFieldPaletteFirstFrame), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldPaletteSecondFrame))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x4);
    if (m.flags() & emu::CF)
        goto branch_a79c;
    m.set_al(uint8_t(0x0));
branch_a79c:;
    m.wr8(m.ss(), uint16_t(kFieldPaletteSecondFrame), uint8_t(m.al()));
    m.set_al(uint8_t(0xCF));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.ss());
    m.es() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldPaletteSecondFrame))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x4)));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x5));
    m.ax() =
        uint16_t(computed::add<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kFieldAnimationPaletteSource))));
    m.si() = uint16_t(m.ax());
    m.di() = uint16_t((kPaletteBuffer + 0x60));
    m.cx() = uint16_t(0x10);
    computed::copy_words(m);
branch_a7c7:;
    return;
}
void animate_field_palette_three_four_frames(Machine &m) {
    animate_field_palette_three_four_frames_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x35A7C8 (near)
// Cycle palette slot three through two animation frames.
void animate_field_palette_three_two_frames_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldPaletteTimer))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ss(), uint16_t(kFieldPaletteTimer), uint8_t(m.al()));
    if (!(m.flags() & emu::ZF))
        goto branch_a821;
    m.set_al(uint8_t(0x8));
    m.wr8(m.ss(), uint16_t(kFieldPaletteTimer), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldPaletteFirstFrame))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::CF)
        goto branch_a7e6;
    m.set_al(uint8_t(0x0));
branch_a7e6:;
    m.wr8(m.ss(), uint16_t(kFieldPaletteFirstFrame), uint8_t(m.al()));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldPaletteSecondFrame))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x2);
    if (m.flags() & emu::CF)
        goto branch_a7f6;
    m.set_al(uint8_t(0x0));
branch_a7f6:;
    m.wr8(m.ss(), uint16_t(kFieldPaletteSecondFrame), uint8_t(m.al()));
    m.set_al(uint8_t(0xCF));
    m.out8(uint8_t(0xC2), m.al());
    m.ax() = uint16_t(0x2000);
    m.ds() = uint16_t(m.ax());
    m.ax() = uint16_t(m.ss());
    m.es() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kFieldPaletteFirstFrame))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), 0x4)));
    m.ax() = uint16_t(computed::shift_left<uint16_t>(m, m.ax(), 0x5));
    m.ax() =
        uint16_t(computed::add<uint16_t>(m, m.ax(), m.rd16(m.ss(), uint16_t(kFieldAnimationPaletteSource))));
    m.si() = uint16_t(m.ax());
    m.di() = uint16_t((kPaletteBuffer + 0x60));
    m.cx() = uint16_t(0x10);
    computed::copy_words(m);
branch_a821:;
    return;
}
void animate_field_palette_three_two_frames(Machine &m) {
    animate_field_palette_three_two_frames_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x35BC73
// Heal a conscious, unstoned member by the item's base HP plus a random bonus.
void use_field_potion_core(Machine &m) {
    (void)computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ds(), uint16_t(m.di())) & 0x3));
    if (!(m.flags() & emu::ZF))
        goto branch_bc99;
    computed::push(m, m.bx());
    m.set_al(uint8_t(m.rd8(m.es(), uint16_t(m.bx() + 0x0004))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.es(), uint16_t(m.bx() + 0x0005))));
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.bx() = uint16_t(
        (m.bx() & 0x00FF) |
        (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx() >> 8) ^ uint8_t(m.bx() >> 8)))))
         << 8));
    {
        const uint16_t callback_cs = m.cs();
        add_field_healing_hp_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.bx() = uint16_t(computed::pop(m));
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.set_al(uint8_t(m.rd8(m.es(), uint16_t(m.bx() + 0x0007))));
    m.ax() = uint16_t(0x11);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        play_menu_sound_preserving_registers_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.flags() &= ~emu::CF;
    return;
branch_bc99:;
    m.ax() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_unusable_item_message_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.flags() |= emu::CF;
    return;
}
void use_field_potion(Machine &m) {
    use_field_potion_core(m);
    if (m.cs() == 0x5000)
        m.retf();
}
// rom: 0x35BCA3
// Apply the common potion healing effect for a hi-potion.
void use_field_hi_potion_core(Machine &m) {
    {
        const uint16_t callback_cs = m.cs();
        use_field_potion_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    return;
}
void use_field_hi_potion(Machine &m) {
    use_field_hi_potion_core(m);
    if (m.cs() == 0x5000)
        m.retf();
}
// rom: 0x35BCA9
// Heal an eligible member by a byte-wrapped sum of item power, caster/target magic and a random bonus.
void use_field_cure_book_core(Machine &m) {
    (void)computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ds(), uint16_t(m.di())) & 0x3));
    if (!(m.flags() & emu::ZF))
        goto branch_bcec;
    m.ax() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.ax() ^ m.ax())));
    m.dx() = uint16_t(computed::logic<uint16_t>(m, uint16_t(m.dx() ^ m.dx())));
    m.set_al(uint8_t(m.rd8(m.es(), uint16_t(m.bx() + 0x0004))));
    m.set_al(uint8_t(computed::shift_right<uint8_t>(m, m.al(), 0x2)));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.bp() + 0x000E))));
    {
        const uint16_t product = uint16_t(m.al()) * uint8_t(m.dx());
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    // Original-game bug kept: the random upper bound is only the low byte of (power/4)*caster magic.
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
    // Original-game bug kept: roll + caster magic + target magic + power wraps at 256 before HP is added.
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), m.rd8(m.ss(), uint16_t(m.bp() + 0x000E)))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), m.rd8(m.ss(), uint16_t(m.di() + 0x000E)))));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), m.rd8(m.es(), uint16_t(m.bx() + 0x0004)))));
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    computed::push(m, m.bx());
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        add_field_healing_hp_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.bx() = uint16_t(computed::pop(m));
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.set_al(uint8_t(m.rd8(m.es(), uint16_t(m.bx() + 0x0007))));
    m.ax() = uint16_t(0x11);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        play_menu_sound_preserving_registers_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.flags() &= ~emu::CF;
    return;
branch_bcec:;
    m.ax() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_unusable_item_message_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.flags() |= emu::CF;
    return;
}
void use_field_cure_book(Machine &m) {
    use_field_cure_book_core(m);
    if (m.cs() == 0x5000)
        m.retf();
}
// rom: 0x35BCF6
// Apply the magic-scaled field healing effect for a healing staff.
void use_field_healing_staff_core(Machine &m) {
    {
        const uint16_t callback_cs = m.cs();
        use_field_cure_book_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    return;
}
void use_field_healing_staff(Machine &m) {
    use_field_healing_staff_core(m);
    if (m.cs() == 0x5000)
        m.retf();
}
// rom: 0x35BCFC
// Clear the status bits selected by the item on a living member.
void use_field_gold_needle_core(Machine &m) {
    (void)computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ds(), uint16_t(m.di())) & 0x1));
    if (!(m.flags() & emu::ZF))
        goto branch_bd19;
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.di()))));
    m.set_al(
        uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & m.rd8(m.es(), uint16_t(m.bx() + 0x0004))))));
    m.wr8(m.ds(), uint16_t(m.di()), uint8_t(m.al()));
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.set_al(uint8_t(m.rd8(m.es(), uint16_t(m.bx() + 0x0007))));
    m.ax() = uint16_t(0x11);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        play_menu_sound_preserving_registers_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.flags() &= ~emu::CF;
    return;
branch_bd19:;
    m.ax() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_unusable_item_message_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.flags() |= emu::CF;
    return;
}
void use_field_gold_needle(Machine &m) {
    use_field_gold_needle_core(m);
    if (m.cs() == 0x5000)
        m.retf();
}
// rom: 0x35BD23
// Apply the shared status-clearing effect for a cross.
void use_field_cross_core(Machine &m) {
    {
        const uint16_t callback_cs = m.cs();
        use_field_gold_needle_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    return;
}
void use_field_cross(Machine &m) {
    use_field_cross_core(m);
    if (m.cs() == 0x5000)
        m.retf();
}
// rom: 0x35BD29
// Apply the shared status-clearing effect for eye drops.
void use_field_eye_drops_core(Machine &m) {
    {
        const uint16_t callback_cs = m.cs();
        use_field_gold_needle_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    return;
}
void use_field_eye_drops(Machine &m) {
    use_field_eye_drops_core(m);
    if (m.cs() == 0x5000)
        m.retf();
}
// rom: 0x35BD2F
// Spend one life and clear the item-selected status bits on a dead member; write 1 to the low HP byte.
void use_field_revive_item_core(Machine &m) {
    (void)computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ds(), uint16_t(m.di())) & 0x1));
    if (m.flags() & emu::ZF)
        goto branch_bd60;
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.di() + 0x000A))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0xF))));
    if (m.flags() & emu::ZF)
        goto branch_bd60;
    if (hooks::should(hooks::VB::SpendHeart, m, true, {0}))
        m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al(), true)));
    m.wr8(m.ds(), uint16_t(m.di() + 0x000A),
          uint8_t(computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ds(), uint16_t(m.di() + 0x000A)) & 0xF0))));
    m.wr8(m.ds(), uint16_t(m.di() + 0x000A),
          uint8_t(computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ds(), uint16_t(m.di() + 0x000A)) | m.al()))));
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.di()))));
    m.set_al(
        uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & m.rd8(m.es(), uint16_t(m.bx() + 0x0004))))));
    m.wr8(m.ds(), uint16_t(m.di()), uint8_t(m.al()));
    // Original-game bug kept: only HP's low byte is set to 1; a nonzero old high byte survives.
    m.wr8(m.ds(), uint16_t(m.di() + 0x0006), uint8_t(0x1));
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.set_al(uint8_t(m.rd8(m.es(), uint16_t(m.bx() + 0x0007))));
    m.ax() = uint16_t(0x5D);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        play_menu_sound_preserving_registers_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.flags() &= ~emu::CF;
    return;
branch_bd60:;
    m.ax() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_unusable_item_message_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.flags() |= emu::CF;
    return;
}
void use_field_revive_item(Machine &m) {
    use_field_revive_item_core(m);
    if (m.cs() == 0x5000)
        m.retf();
}
// rom: 0x35BD6A
// Clear selected statuses, restore HP and refill ability uses for a living member.
void use_field_elixir_core(Machine &m) {
    (void)computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ds(), uint16_t(m.di())) & 0x1));
    if (!(m.flags() & emu::ZF))
        goto branch_bdbd;
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.di()))));
    m.set_al(
        uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & m.rd8(m.es(), uint16_t(m.bx() + 0x0004))))));
    m.wr8(m.ds(), uint16_t(m.di()), uint8_t(m.al()));
    m.ax() = uint16_t(0x3E7);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        add_field_healing_hp_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    computed::push(m, m.bx());
    computed::push(m, m.di());
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0xF));
    m.cx() = uint16_t(0x8);
branch_bd88:;
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.ds(), uint16_t(m.di()))));
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0x80);
    if (m.flags() & emu::CF)
        goto branch_bda6;
    (void)computed::subtract<uint8_t>(m, uint8_t(m.bx()), 0xFC);
    if (!(m.flags() & emu::CF))
        goto branch_bda6;
    m.bx() = uint16_t(
        (m.bx() & 0x00FF) |
        (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx() >> 8) ^ uint8_t(m.bx() >> 8)))))
         << 8));
    m.bx() = uint16_t(computed::shift_left<uint16_t>(m, m.bx(), 0x1));
    m.bx() = uint16_t(computed::add<uint16_t>(m, m.bx(), 0x41E0));
    m.bx() = uint16_t(m.rd16(m.es(), uint16_t(m.bx())));
    m.set_al(uint8_t(table_at(m, Table::item_records, m.es(), uint16_t(m.bx() + 0x0003))));
    m.wr8(m.ds(), uint16_t(m.di() + 0x0001), uint8_t(m.al()));
branch_bda6:;
    m.di() = uint16_t(computed::add<uint16_t>(m, m.di(), 0x2));
    if (--m.cx())
        goto branch_bd88;
    m.di() = uint16_t(computed::pop(m));
    m.bx() = uint16_t(computed::pop(m));
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.set_al(uint8_t(m.rd8(m.es(), uint16_t(m.bx() + 0x0007))));
    m.ax() = uint16_t(0x11);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        play_menu_sound_preserving_registers_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.flags() &= ~emu::CF;
    return;
branch_bdbd:;
    m.ax() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_unusable_item_message_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.flags() |= emu::CF;
    return;
}
void use_field_elixir(Machine &m) {
    use_field_elixir_core(m);
    if (m.cs() == 0x5000)
        m.retf();
}
// rom: 0x35BDC7
// Increase the member's remaining lives up to nine.
void use_field_heart_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.di() + 0x000A))));
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() & 0xF))));
    m.set_al(uint8_t(computed::increment<uint8_t>(m, m.al())));
    (void)computed::subtract<uint8_t>(m, m.al(), 0x9);
    if (m.flags() & (emu::CF | emu::ZF))
        goto branch_bdd4;
    m.set_al(uint8_t(0x9));
branch_bdd4:;
    m.wr8(m.ds(), uint16_t(m.di() + 0x000A),
          uint8_t(computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ds(), uint16_t(m.di() + 0x000A)) & 0xF0))));
    m.wr8(m.ds(), uint16_t(m.di() + 0x000A),
          uint8_t(computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ds(), uint16_t(m.di() + 0x000A)) | m.al()))));
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.set_al(uint8_t(m.rd8(m.es(), uint16_t(m.bx() + 0x0007))));
    m.ax() = uint16_t(0x11);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        play_menu_sound_preserving_registers_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.flags() &= ~emu::CF;
    return;
}
void use_field_heart(Machine &m) {
    use_field_heart_core(m);
    if (m.cs() == 0x5000)
        m.retf();
}
// rom: 0x35BDED
// Return success without changing field state for the sage's stone.
void use_field_sages_stone_core(Machine &m) {
    m.flags() &= ~emu::CF;
    return;
}
void use_field_sages_stone(Machine &m) {
    use_field_sages_stone_core(m);
    if (m.cs() == 0x5000)
        m.retf();
}
// rom: 0x35BE38
// Increase a conscious, unstoned human's strength by item power plus a stat-scaled roll, capped at 99.
void use_field_strength_source_core(Machine &m) {
    (void)computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ds(), uint16_t(m.di() + 0x000A)) & 0x80));
    if (m.flags() & emu::ZF)
        goto branch_be85;
    (void)computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ds(), uint16_t(m.di())) & 0x3));
    if (!(m.flags() & emu::ZF))
        goto branch_be85;
    m.set_ah(uint8_t(m.rd8(m.es(), uint16_t(m.bx() + 0x0005))));
    m.set_al(uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    computed::push(m, m.bx());
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.di() + 0x000B))));
    {
        const uint16_t callback_cs = m.cs();
        divide_growth_bonus_by_stat_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    // Gain = item power + floor(roll(0..item spread)/current strength); zero strength gets no random bonus.
    m.set_al(uint8_t(uint8_t(m.cx())));
    m.bx() = uint16_t(computed::pop(m));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), m.rd8(m.es(), uint16_t(m.bx() + 0x0004)))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.bx() = uint16_t(
        (m.bx() & 0x00FF) |
        (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx() >> 8) ^ uint8_t(m.bx() >> 8)))))
         << 8));
    m.bx() = uint16_t(std::clamp<int32_t>(hooks::filter(hooks::Value::StatGain, m, m.bx(), {0xB, 1}), 0, 99));
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.di() + 0x000B))));
    {
        const uint16_t callback_cs = m.cs();
        clamp_field_stat_increase_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.wr8(m.ss(), uint16_t(m.di() + 0x000B), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        show_strength_increase_message_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.set_al(uint8_t(m.rd8(m.es(), uint16_t(m.bx() + 0x0007))));
    m.ax() = uint16_t(0x11);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        play_menu_sound_preserving_registers_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.flags() &= ~emu::CF;
    return;
branch_be85:;
    m.ax() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_unusable_item_message_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.flags() |= emu::CF;
    return;
}
void use_field_strength_source(Machine &m) {
    use_field_strength_source_core(m);
    if (m.cs() == 0x5000)
        m.retf();
}
// rom: 0x35BE8F
// Increase a conscious, unstoned human's agility by item power plus a stat-scaled roll, capped at 99.
void use_field_agility_source_core(Machine &m) {
    (void)computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ds(), uint16_t(m.di() + 0x000A)) & 0x80));
    if (m.flags() & emu::ZF)
        goto branch_beda;
    (void)computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ds(), uint16_t(m.di())) & 0x3));
    if (!(m.flags() & emu::ZF))
        goto branch_beda;
    m.set_ah(uint8_t(m.rd8(m.es(), uint16_t(m.bx() + 0x0005))));
    m.set_al(uint8_t(0x0));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    computed::push(m, m.bx());
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(m.rd8(m.ss(), uint16_t(m.di() + 0x000D))));
    {
        const uint16_t callback_cs = m.cs();
        divide_growth_bonus_by_stat_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    // Gain = item power + floor(roll(0..item spread)/current agility); zero agility gets no random bonus.
    m.set_al(uint8_t(uint8_t(m.cx())));
    m.bx() = uint16_t(computed::pop(m));
    m.set_al(uint8_t(computed::add<uint8_t>(m, m.al(), m.rd8(m.es(), uint16_t(m.bx() + 0x0004)))));
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.al()));
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.bx() = uint16_t(
        (m.bx() & 0x00FF) |
        (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx() >> 8) ^ uint8_t(m.bx() >> 8)))))
         << 8));
    m.bx() = uint16_t(std::clamp<int32_t>(hooks::filter(hooks::Value::StatGain, m, m.bx(), {0xD, 1}), 0, 99));
    m.set_al(uint8_t(m.rd8(m.ds(), uint16_t(m.di() + 0x000D))));
    {
        const uint16_t callback_cs = m.cs();
        clamp_field_stat_increase_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.wr8(m.ds(), uint16_t(m.di() + 0x000D), uint8_t(m.al()));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        show_agility_increase_message_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.set_al(uint8_t(m.rd8(m.es(), uint16_t(m.bx() + 0x0007))));
    m.ax() = uint16_t(0x11);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        play_menu_sound_preserving_registers_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.flags() &= ~emu::CF;
    return;
branch_beda:;
    m.ax() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_unusable_item_message_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.flags() |= emu::CF;
    return;
}
void use_field_agility_source(Machine &m) {
    use_field_agility_source_core(m);
    if (m.cs() == 0x5000)
        m.retf();
}
// rom: 0x35BEE4 (near)
// Add BX to AX up to 99, leaving the actual increase in BX.
void clamp_field_stat_increase_core(Machine &m) {
    (void)computed::subtract<uint16_t>(m, m.ax(), 0x63);
    if (!(m.flags() & emu::CF))
        goto branch_befa;
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.bx()));
    (void)computed::subtract<uint16_t>(m, m.ax(), 0x63);
    if (m.flags() & (emu::CF | emu::ZF))
        goto branch_befd;
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), 0x63));
    m.bx() = uint16_t(computed::subtract<uint16_t>(m, m.bx(), m.ax()));
    m.ax() = uint16_t(0x63);
    goto branch_befd;
branch_befa:;
    m.bx() = uint16_t(0x0);
branch_befd:;
    return;
}
void clamp_field_stat_increase(Machine &m) {
    clamp_field_stat_increase_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x35BEFE (near)
// Divide BL's random growth bonus by DL's current stat, returning the quotient in CL.
void divide_growth_bonus_by_stat_core(Machine &m) {
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(0x0));
    m.dx() = uint16_t((m.dx() & 0xFF00) |
                      uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.dx()) | uint8_t(m.dx())))));
    if (!(m.flags() & emu::ZF))
        goto branch_bf05;
    return;
// Eight restoring-division steps compute floor(BL/DL); the zero-stat path leaves CL = 0.
branch_bf05:;
    m.set_al(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.al() ^ m.al()))));
    m.dx() = uint16_t((m.dx() & 0x00FF) | (uint16_t(uint8_t(0x8)) << 8));
branch_bf09:;
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(computed::rotate_left<uint8_t>(m, uint8_t(m.bx()), 0x1)));
    m.set_al(uint8_t(computed::rotate_left<uint8_t>(m, m.al(), 0x1, true)));
    (void)computed::subtract<uint8_t>(m, m.al(), uint8_t(m.dx()));
    if (m.flags() & emu::CF)
        goto branch_bf13;
    m.set_al(uint8_t(computed::subtract<uint8_t>(m, m.al(), uint8_t(m.dx()))));
branch_bf13:;
    m.flags() ^= emu::CF;
    m.cx() =
        uint16_t((m.cx() & 0xFF00) | uint8_t(computed::rotate_left<uint8_t>(m, uint8_t(m.cx()), 0x1, true)));
    m.dx() = uint16_t((m.dx() & 0x00FF) |
                      (uint16_t(uint8_t(computed::increment<uint8_t>(m, uint8_t(m.dx() >> 8), true))) << 8));
    if (!(m.flags() & emu::ZF))
        goto branch_bf09;
    return;
}
void divide_growth_bonus_by_stat(Machine &m) {
    divide_growth_bonus_by_stat_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x35BF1B
// Increase eligible human max HP by 5 plus a roll while below the item threshold, otherwise by one.
void use_field_hp_two_hundred_core(Machine &m) {
    (void)computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ds(), uint16_t(m.di() + 0x000A)) & 0x80));
    if (m.flags() & emu::ZF)
        goto branch_bf53;
    (void)computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ds(), uint16_t(m.di())) & 0x3));
    if (!(m.flags() & emu::ZF))
        goto branch_bf53;
    m.set_al(uint8_t(m.rd8(m.es(), uint16_t(m.bx() + 0x0004))));
    m.dx() = uint16_t((m.dx() & 0xFF00) | uint8_t(0xA));
    {
        const uint16_t product = uint16_t(m.al()) * uint8_t(m.dx());
        m.ax() = product;
        computed::flag(m, emu::CF | emu::OF, product > 255);
    }
    m.cx() = uint16_t(m.ax());
    computed::push(m, m.bx());
    m.ax() = uint16_t(0x5);
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.rd8(m.es(), uint16_t(m.bx() + 0x0005))));
    m.bx() = uint16_t(
        (m.bx() & 0x00FF) |
        (uint16_t(uint8_t(computed::logic<uint8_t>(m, uint8_t(uint8_t(m.bx() >> 8) ^ uint8_t(m.bx() >> 8)))))
         << 8));
    {
        const uint16_t callback_cs = m.cs();
        add_field_maximum_hp_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        show_max_hp_increase_message_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(computed::pop(m));
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.set_al(uint8_t(m.rd8(m.es(), uint16_t(m.bx() + 0x0007))));
    m.ax() = uint16_t(0x11);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        play_menu_sound_preserving_registers_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.flags() &= ~emu::CF;
    return;
branch_bf53:;
    m.ax() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_unusable_item_message_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.flags() |= emu::CF;
    return;
}
void use_field_hp_two_hundred(Machine &m) {
    use_field_hp_two_hundred_core(m);
    if (m.cs() == 0x5000)
        m.retf();
}
// rom: 0x35BF5D
// Apply the shared maximum-HP growth effect for HP400.
void use_field_hp_four_hundred_core(Machine &m) {
    {
        const uint16_t callback_cs = m.cs();
        use_field_hp_two_hundred_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    return;
}
void use_field_hp_four_hundred(Machine &m) {
    use_field_hp_four_hundred_core(m);
    if (m.cs() == 0x5000)
        m.retf();
}
// rom: 0x35BF63
// Apply the shared maximum-HP growth effect for HP600.
void use_field_hp_six_hundred_core(Machine &m) {
    {
        const uint16_t callback_cs = m.cs();
        use_field_hp_two_hundred_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    return;
}
void use_field_hp_six_hundred(Machine &m) {
    use_field_hp_six_hundred_core(m);
    if (m.cs() == 0x5000)
        m.retf();
}
// rom: 0x35BF69
// Clear poison on a living member.
void use_field_antidote_core(Machine &m) {
    (void)computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ds(), uint16_t(m.di())) & 0x1));
    if (!(m.flags() & emu::ZF))
        goto branch_bf7b;
    m.wr8(m.ds(), uint16_t(m.di()),
          uint8_t(computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ds(), uint16_t(m.di())) & 0xEF))));
    m.ax() = uint16_t(0x11);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        play_menu_sound_preserving_registers_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.flags() &= ~emu::CF;
    return;
branch_bf7b:;
    m.ax() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_unusable_item_message_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.flags() |= emu::CF;
    return;
}
void use_field_antidote(Machine &m) {
    use_field_antidote_core(m);
    if (m.cs() == 0x5000)
        m.retf();
}
// rom: 0x35BF85
// Clear sleep on a living member.
void use_field_wake_bell_core(Machine &m) {
    (void)computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ds(), uint16_t(m.di())) & 0x1));
    if (!(m.flags() & emu::ZF))
        goto branch_bf97;
    m.wr8(m.ds(), uint16_t(m.di()),
          uint8_t(computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ds(), uint16_t(m.di())) & 0x7F))));
    m.ax() = uint16_t(0x11);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        play_menu_sound_preserving_registers_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.flags() &= ~emu::CF;
    return;
branch_bf97:;
    m.ax() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_unusable_item_message_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.flags() |= emu::CF;
    return;
}
void use_field_wake_bell(Machine &m) {
    use_field_wake_bell_core(m);
    if (m.cs() == 0x5000)
        m.retf();
}
// rom: 0x35BFA1
// Clear confusion on a living member.
void use_field_frying_pan_core(Machine &m) {
    (void)computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ds(), uint16_t(m.di())) & 0x1));
    if (!(m.flags() & emu::ZF))
        goto branch_bfb3;
    m.wr8(m.ds(), uint16_t(m.di()),
          uint8_t(computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ds(), uint16_t(m.di())) & 0xDF))));
    m.ax() = uint16_t(0x11);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        play_menu_sound_preserving_registers_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.flags() &= ~emu::CF;
    return;
branch_bfb3:;
    m.ax() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_unusable_item_message_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.flags() |= emu::CF;
    return;
}
void use_field_frying_pan(Machine &m) {
    use_field_frying_pan_core(m);
    if (m.cs() == 0x5000)
        m.retf();
}
// rom: 0x35BFBD
// Clear paralysis on a living member.
void use_field_paralysis_cure_core(Machine &m) {
    (void)computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ds(), uint16_t(m.di())) & 0x1));
    if (!(m.flags() & emu::ZF))
        goto branch_bfcf;
    m.wr8(m.ds(), uint16_t(m.di()),
          uint8_t(computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ds(), uint16_t(m.di())) & 0xBF))));
    m.ax() = uint16_t(0x11);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        play_menu_sound_preserving_registers_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.flags() &= ~emu::CF;
    return;
branch_bfcf:;
    m.ax() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_unusable_item_message_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.flags() |= emu::CF;
    return;
}
void use_field_paralysis_cure(Machine &m) {
    use_field_paralysis_cure_core(m);
    if (m.cs() == 0x5000)
        m.retf();
}
// rom: 0x35BFD9
// Apply common potion healing for nectar.
void use_field_nectar_core(Machine &m) {
    {
        const uint16_t callback_cs = m.cs();
        use_field_potion_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    return;
}
void use_field_nectar(Machine &m) {
    use_field_nectar_core(m);
    if (m.cs() == 0x5000)
        m.retf();
}
// rom: 0x35BFDF
// Apply magic-scaled field healing for the cure spell.
void use_field_cure_spell_core(Machine &m) {
    {
        const uint16_t callback_cs = m.cs();
        use_field_cure_book_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    return;
}
void use_field_cure_spell(Machine &m) {
    use_field_cure_spell_core(m);
    if (m.cs() == 0x5000)
        m.retf();
}
// rom: 0x35BFE5
// Apply magic-scaled field healing for the healing spell.
void use_field_healing_spell_core(Machine &m) {
    {
        const uint16_t callback_cs = m.cs();
        use_field_cure_book_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    return;
}
void use_field_healing_spell(Machine &m) {
    use_field_healing_spell_core(m);
    if (m.cs() == 0x5000)
        m.retf();
}
// rom: 0x35BFEB
// Restore a living member's HP to its maximum.
void use_field_revival_spell_core(Machine &m) {
    (void)computed::logic<uint8_t>(m, uint8_t(m.rd8(m.ds(), uint16_t(m.di())) & 0x1));
    if (!(m.flags() & emu::ZF))
        goto branch_c009;
    m.ax() = uint16_t(0x3E7);
    m.bx() = uint16_t(0x0);
    {
        const uint16_t callback_cs = m.cs();
        add_field_healing_hp_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.set_ah(uint8_t(computed::logic<uint8_t>(m, uint8_t(m.ah() ^ m.ah()))));
    m.set_al(uint8_t(m.rd8(m.es(), uint16_t(m.bx() + 0x0007))));
    m.ax() = uint16_t(0x11);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        play_menu_sound_preserving_registers_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.flags() &= ~emu::CF;
    return;
branch_c009:;
    m.ax() = uint16_t(0x0);
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_unusable_item_message_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.flags() |= emu::CF;
    return;
}
void use_field_revival_spell(Machine &m) {
    use_field_revival_spell_core(m);
    if (m.cs() == 0x5000)
        m.retf();
}
// rom: 0x35C013
// Apply the shared status-clearing effect for treatment.
void use_field_treatment_spell_core(Machine &m) {
    {
        const uint16_t callback_cs = m.cs();
        use_field_gold_needle_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    return;
}
void use_field_treatment_spell(Machine &m) {
    use_field_treatment_spell_core(m);
    if (m.cs() == 0x5000)
        m.retf();
}
// rom: 0x35C019
// Apply the shared life-consuming revival effect for raise.
void use_field_raise_spell_core(Machine &m) {
    {
        const uint16_t callback_cs = m.cs();
        use_field_revive_item_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    return;
}
void use_field_raise_spell(Machine &m) {
    use_field_raise_spell_core(m);
    if (m.cs() == 0x5000)
        m.retf();
}
// rom: 0x35C01F
// Return success without a field-state change for this effect slot.
void ignore_field_teleport_spell_core(Machine &m) {
    m.flags() &= ~emu::CF;
    return;
}
void ignore_field_teleport_spell(Machine &m) {
    ignore_field_teleport_spell_core(m);
    if (m.cs() == 0x5000)
        m.retf();
}
// rom: 0x35C021 (near)
// Add AX plus a random bonus to the member's HP, clamped to maximum HP.
void add_field_healing_hp_core(Machine &m) {
    {
        const uint16_t callback_cs = m.cs();
        random_field_healing_bonus_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.bx()));
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.rd16(m.ds(), uint16_t(m.di() + 0x0006))));
    (void)computed::subtract<uint16_t>(m, m.ax(), m.rd16(m.ds(), uint16_t(m.di() + 0x0008)));
    if (m.flags() & (emu::CF | emu::ZF))
        goto branch_c031;
    m.ax() = uint16_t(m.rd16(m.ds(), uint16_t(m.di() + 0x0008)));
branch_c031:;
    m.wr16(m.ds(), uint16_t(m.di() + 0x0006), uint16_t(m.ax()));
    return;
}
void add_field_healing_hp(Machine &m) {
    add_field_healing_hp_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x35C035 (near)
// Add base plus random HP below CX's threshold, otherwise one; cap at 999 and return actual gain in BX.
void add_field_maximum_hp_core(Machine &m) {
    {
        const uint16_t callback_cs = m.cs();
        random_field_healing_bonus_core(m);
        if (m.cs() != callback_cs)
            return;
    }
    (void)computed::subtract<uint16_t>(m, m.cx(), m.rd16(m.ds(), uint16_t(m.di() + 0x0008)));
    // At or above the item threshold, the gain is exactly 1; below it, use base plus the inclusive random
    // bonus.
    if (!(m.flags() & (emu::CF | emu::ZF)))
        goto branch_c047;
    m.ax() = uint16_t(0x1);
    m.bx() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.rd16(m.ds(), uint16_t(m.di() + 0x0008))));
    goto branch_c04e;
branch_c047:;
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.bx()));
    m.bx() = uint16_t(m.ax());
    m.ax() = uint16_t(computed::add<uint16_t>(m, m.ax(), m.rd16(m.ds(), uint16_t(m.di() + 0x0008))));
branch_c04e:;
    if (const int32_t gain = hooks::filter(hooks::Value::StatGain, m, m.bx(), {8, 1}); gain != m.bx()) {
        m.bx() = uint16_t(std::clamp<int32_t>(gain, 0, 999)); // Stat Growth
        m.ax() = uint16_t(m.bx() + m.rd16(m.ds(), uint16_t(m.di() + 0x0008)));
    }
    (void)computed::subtract<uint16_t>(m, m.ax(), 0x3E7);
    if (m.flags() & (emu::CF | emu::ZF))
        goto branch_c05b;
    // Subtract overflow above 999 from the reported gain, then store the capped maximum HP.
    m.ax() = uint16_t(computed::subtract<uint16_t>(m, m.ax(), 0x3E7));
    m.bx() = uint16_t(computed::subtract<uint16_t>(m, m.bx(), m.ax()));
    m.ax() = uint16_t(0x3E7);
branch_c05b:;
    m.wr16(m.ds(), uint16_t(m.di() + 0x0008), uint16_t(m.ax()));
    return;
}
void add_field_maximum_hp(Machine &m) {
    add_field_maximum_hp_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
// rom: 0x35C05F (near)
// Return an inclusive 0..BL random bonus in BX, preserving AX, CX, SI and DI.
void random_field_healing_bonus_core(Machine &m) {
    computed::push(m, m.ax());
    computed::push(m, m.cx());
    computed::push(m, m.si());
    computed::push(m, m.di());
    m.set_al(uint8_t(0x0));
    m.set_ah(uint8_t(uint8_t(m.bx())));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        random_range_core(m);
        if (m.cs() != 0x4000)
            return;
        m.cs() = caller_cs;
    }
    m.bx() = uint16_t(m.ax());
    m.di() = uint16_t(computed::pop(m));
    m.si() = uint16_t(computed::pop(m));
    m.cx() = uint16_t(computed::pop(m));
    m.ax() = uint16_t(computed::pop(m));
    return;
}
void random_field_healing_bonus(Machine &m) {
    random_field_healing_bonus_core(m);
    if (m.cs() == 0x5000)
        m.ret();
}
} // namespace sawgod::game
