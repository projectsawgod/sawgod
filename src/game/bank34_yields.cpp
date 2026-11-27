#include "game/bank34_misc.h"
#include "game/bank35_world.h"
#include "game/bank36_misc.h"
#include "game/computed_handlers.h"
#include "game/hooks.h"
#include "game/party_menu.h"
#include "game/save_slots.h"
#include "game/session.h"
#include "game/tables.h"
#include "game/text.h"
#include "game/ui_scene.h"
// Frame-spanning menu and creation routines from bank 34 (segment 4000h).
#include "game/bank34.h"
#include "game/bank3d_sound.h"
#include <bit>
#include <initializer_list>
namespace sawgod::game {
namespace {
constexpr uint16_t kMenuCursorPosition = 0x014A;
constexpr uint16_t kScreenMap2 = 0x1800;
constexpr uint16_t kMenuScreenBackup = 0x2800;
constexpr uint16_t kSpriteBuffer = 0xC000;
constexpr uint16_t kSpriteBackup = 0xC200; // sprite_backup
constexpr uint16_t kPaletteBuffer = 0xC400;
constexpr uint16_t kPaletteTargetBuffer = 0xC600;
constexpr uint16_t kKeysPressed = 0xC804;
constexpr uint16_t kKeysRepeat = 0xC806;
constexpr uint16_t kGameState = 0xD000;
constexpr uint16_t kMember0Equipment = 0xD00F;
constexpr uint16_t kInventory = 0xD07C;
constexpr uint16_t kKeyItems = 0xD08C;
constexpr uint16_t kTowerCounter = 0xD0DA;
constexpr uint16_t kCurrentFloor = 0xD0DB;      // current_floor
constexpr uint16_t kSaveFromPartyMenu = 0xD0E4; // save_from_party_menu
constexpr uint16_t kDialogueSpeed = 0xD0E6;
constexpr uint16_t kPartyOrder = 0xD0E8;
constexpr uint16_t kPartyOrderLast = 0xD0EB;
constexpr uint16_t kLineScrollTable = 0xD6FE;
constexpr uint16_t kPartyMenuEntryContext = 0xD700;
constexpr uint16_t kMenuDescriptionMode = 0xD83F; // menu_description_mode
constexpr uint16_t kTabActionIndex = 0xD840;
constexpr uint16_t kRaceSelectIndex = 0xD841;
constexpr uint16_t kTabClassSelectIndex =
    0xD842; // creation class/grid column; item destination row; save slot
constexpr uint16_t kTabGenderSelectIndex = 0xD843; // creation gender/grid row; second-list row; save yes/no
constexpr uint16_t kTabThirdSelection = 0xD844;
constexpr uint16_t kTabFourthSelection = 0xD845;
constexpr uint16_t kTabFifthSelection = 0xD846;
constexpr uint16_t kTabSixthSelection = 0xD847;
constexpr uint16_t kTabExitRequested = 0xD84A;
constexpr uint16_t kTabActionResult = 0xD84B;
constexpr uint16_t kTabListColumn = 0xD852;      // creation leader; item-menu list column
constexpr uint16_t kTabEquipmentMember = 0xD853; // creation race; selected party member in item menus
constexpr uint16_t kTabSourceColumn = 0xD854;    // name-entry alphabet page; item-transfer source column
constexpr uint16_t kTabItemBuffer = 0xD855;      // name insertion index; first transfer item buffer
constexpr uint16_t kNameText = 0xD856;           // name-entry bytes; transfer menus reuse the same workspace
constexpr uint16_t kTabSecondItemBuffer = 0xD857;
constexpr uint16_t kTabTransferState = 0xD859; // last name byte; transfer state / monster-history scratch
constexpr uint16_t kTabTransferCount = 0xD85A; // name caret position; transfer item count
constexpr uint16_t kTabTransferPage = 0xD85B;
constexpr uint16_t kTabTransferResult = 0xD85C;
constexpr uint16_t kSaveScratch = 0xD8FE;
// A host picture may cover a native menu while it polls. Nested dialogs suspend it, and every
// return (including exceptional Fiber teardown) hides it. With no subscriber these are no-ops.
void party_menu(Machine &m, int scene) { hooks::emit(hooks::Event::PartyMenu, m, {scene, scene ? 0 : 2}); }
// The same for a presentation event whose a = 0 means "left": every return sends it.
struct LeaveScope {
    Machine &m;
    hooks::Event event;
    ~LeaveScope() { hooks::emit(event, m, {0}); }
};

void tab_call(Machine &m, uint16_t target);
void menu_field_item(Machine &m, uint16_t target) {
    const uint16_t caller_cs = m.cs();
    m.cs() = 0x5000;
    switch (target) {
    case 0xB9B9:
        use_first_tower_key_core(m);
        break;
    case 0xB9CC:
        use_second_tower_key_core(m);
        break;
    case 0xB9DE:
        use_third_tower_key_core(m);
        break;
    case 0xBA8E:
        use_first_progression_item_core(m);
        break;
    case 0xBA93:
        use_second_progression_item_core(m);
        break;
    case 0xBAFD:
        use_special_progression_item_core(m);
        break;
    }
    m.cs() = caller_cs;
}
void menu_compare(Machine &m, uint16_t left, uint16_t right, bool byte);
void menu_test(Machine &m, uint16_t result);
void menu_push(Machine &m, uint16_t stack_value) {
    m.sp() = uint16_t(m.sp() - 2);
    m.wr16(m.ss(), m.sp(), stack_value);
}
uint16_t menu_pop(Machine &m) {
    uint16_t stack_value = m.rd16(m.ss(), m.sp());
    m.sp() = uint16_t(m.sp() + 2);
    return stack_value;
}
void menu_save(Machine &m) {
    uint16_t sp = m.sp();
    for (uint16_t register_value :
         {m.ax(), m.cx(), m.dx(), m.bx(), sp, m.bp(), m.si(), m.di(), m.ds(), m.es(), m.ss()})
        menu_push(m, register_value);
}
void menu_restore(Machine &m) {
    m.ss() = menu_pop(m);
    m.es() = menu_pop(m);
    m.ds() = menu_pop(m);
    m.di() = menu_pop(m);
    m.si() = menu_pop(m);
    m.bp() = menu_pop(m);
    (void)menu_pop(m);
    m.bx() = menu_pop(m);
    m.dx() = menu_pop(m);
    m.cx() = menu_pop(m);
    m.ax() = menu_pop(m);
}
void menu_out16(Machine &m, uint8_t port, uint16_t value) {
    m.out8(port, uint8_t(value));
    m.out8(uint8_t(port + 1), uint8_t(value >> 8));
}
void menu_sound(Machine &m, uint16_t sound) {
    m.ax() = sound;
    play_sound_effect_core(m, m.al());
}
void menu_present(Machine &m) {
    dma_sprite_table_core(m);
    dma_palettes_core(m);
    m.call_far(0x4000, 9);
}
void menu_cursor_display(Machine &m, uint16_t cursor) {
    m.set_al(0);
    m.out8(5, 0);
    m.set_al(0x18);
    m.out8(6, m.al());
    switch (cursor) {
    case 0x22c2:
        draw_direction_choice_cursor_core(m);
        break;
    case 0x28e4:
        draw_class_or_gender_cursor_core(m);
        break;
    case 0x3688:
        draw_equipment_refill_cursor_core(m);
        break;
    case 0x2941:
        draw_refill_target_cursor_core(m);
        break;
    case 0x2978:
        draw_item_target_cursor_core(m);
        break;
    case 0x36e2:
        draw_equipment_action_cursor_core(m);
        break;
    case 0x4f15:
        draw_class_menu_cursor_core(m);
        break;
    default:
        break;
    }
    dma_sprite_table_core(m);
    m.call_far(0x4000, 9);
    dma_palettes_core(m);
    m.ax() = 7;
    menu_out16(m, 0, m.ax());
    m.ax() = 8;
    menu_out16(m, 0x10, m.ax());
    m.ax() = 0xFC08;
    menu_out16(m, 0x12, m.ax());
    m.set_al(0x32);
    m.out8(7, m.al());
}
// Move one menu byte at seg:SI (DS unless given), wrapping across [0, maximum]. AL retains the selected byte.
void menu_move(Machine &m, bool decrement, uint8_t maximum, int seg = -1) {
    const uint16_t selection_segment = seg < 0 ? m.ds() : uint16_t(seg);
    uint8_t selection = m.rd8(selection_segment, m.si());
    if (decrement)
        selection = selection ? uint8_t(selection - 1) : maximum;
    else {
        selection = uint8_t(selection + 1);
        if (selection > maximum)
            selection = 0;
    }
    m.set_al(selection);
    m.wr8(selection_segment, m.si(), selection);
}
} // namespace
// rom: 0x341125 (near)
// Darken all sixteen palettes over sixteen two-frame steps; registers preserved.
void fade_menu_palettes(Machine &m) {
    menu_save(m);
    m.cx() = 16;
    do {
        menu_push(m, m.cx());
        m.ax() = 0;
        m.cx() = 16;
        darken_palette_step_core(m);
        m.cx() = 2;
        wait_frames_core(m);
        dma_palettes_core(m);
        m.cx() = menu_pop(m);
    } while (--m.cx());
    hooks::emit(hooks::Event::TitleMenu, m, {0}); // The title words are now fully faded.
    menu_restore(m);
    m.ret();
}
// Shared body of ROM 0x34198B
// Select one of seven party menu categories, returning after cancel or a submenu requests exit.
void select_party_menu_category_core(Machine &m) {
    UiSceneScope presentation(m, hooks::Event::PartyMenu, 0);
    // Native rows: seven with Config, six without. Host rows are opt-in around native Save.
    const bool config_tab = hooks::should(hooks::VB::ConfigTab, m);
    const PartyMenuRows rows(config_tab, hooks::filter(hooks::Value::PartyMenuRows, m, 0));
    const uint8_t last = uint8_t(rows.count() - 1);
    const auto draw_cursor = [&] {
        // The ROM has only seven sprite anchors. The host picture draws the actual category hand.
        const auto row = m.rd8(m.ss(), kRaceSelectIndex);
        if (rows.hosts)
            m.wr8(m.ss(), kRaceSelectIndex, uint8_t(std::min(int(row), config_tab ? 6 : 5)));
        draw_race_select_cursor_core(m);
        if (rows.hosts)
            m.wr8(m.ss(), kRaceSelectIndex, row);
    };
    m.ax() = 0;
    m.ds() = 0;
    m.set_al(0);
    m.wr8(m.ss(), kRaceSelectIndex, 0);
    bool redraw = true;
    for (;;) {
        if (redraw) {
            m.di() = 0x1A71;
            draw_cursor();
            m.call_far(0x4000, 9);
            draw_party_overview_core(m);
            draw_play_time_core(m);
            m.set_al(0);
            m.out8(5, 0);
            m.set_al(0x18);
            m.out8(6, m.al());
            dma_sprite_table_core(m);
            m.call_far(0x4000, 9);
            dma_palettes_core(m);
            m.ax() = 8;
            menu_out16(m, 0x10, m.ax());
            m.ax() = 0xFC08;
            menu_out16(m, 0x12, m.ax());
            m.set_al(0x32);
            m.out8(7, m.al());
            m.ax() = 7;
            menu_out16(m, 0, m.ax());
            redraw = false;
        }
        draw_play_time_core(m);
        party_menu(m, 1);
        m.call_far(0x4000, 0x5c);
        m.ax() = uint16_t(hooks::filter(hooks::Value::PartyMenuKeys, m, m.rd16(m.ds(), kKeysRepeat)));
        m.dx() = uint16_t((m.dx() & 0xFF00) | 6);
        m.si() = kRaceSelectIndex;
        if (m.ax() & 0xA) {
            menu_sound(m, 0x94);
            return;
        }
        if (m.ax() & 4) {
            menu_sound(m, 0x93);
            m.set_al(m.rd8(m.ss(), kRaceSelectIndex));
            const int row = m.al();
            if (rows.hosts && row == rows.options()) {
                hooks::emit(hooks::Event::PartyMenuAction, m, {0});
                m.ds() = m.ss();
                continue;
            }
            if (rows.hosts == 2 && row == rows.load()) {
                auto &session = Session::of(m.wsc());
                std::array<uint8_t, SaveSlots::kPayload> progress;
                for (size_t i = 0; i < progress.size(); ++i)
                    progress[i] = m.rd8(m.ss(), uint16_t(kGameState + i));
                const auto names = session.player_names().values;
                party_menu(m, 0);
                run_load_tab_core(m);
                m.ds() = m.ss();
                if (!m.rd8(m.ss(), kTabActionResult)) {
                    session.party_load_pending = true;
                    m.wr8(m.ss(), kTabExitRequested, 1);
                    return;
                }
                // New Game+ creation can modify progress before returning to the list. A later B
                // still restores the in-progress game, including full host names.
                for (size_t i = 0; i < progress.size(); ++i)
                    m.wr8(m.ss(), uint16_t(kGameState + i), progress[i]);
                session.player_names().values = names;
                m.wr8(m.ss(), kTabActionResult, 0);
                m.wr8(m.ss(), kTabExitRequested, 0);
                prepare_party_menu_entry_core(m);
                m.wr8(m.ss(), kRaceSelectIndex, uint8_t(row));
                redraw = true;
                party_menu(m, 1);
                m.ds() = m.ss();
                continue;
            }
            m.set_al(uint8_t(rows.native(row)));
            m.set_al(uint8_t(m.al() + 1));
            m.wr8(m.ss(), kMenuDescriptionMode, m.al());
            m.ax() = uint8_t(m.al() - 1);
            m.ax() = uint16_t(m.ax() << 1);
            m.bx() = m.ax();
            m.si() = m.rd16(m.cs(), uint16_t(m.bx() + 0x1A63));
            party_menu(m, 0);
            tab_call(m, m.si());
            m.ds() = m.ss();
            if (m.rd8(m.ss(), kTabExitRequested)) {
                return;
            }
            prepare_party_menu_entry_core(m);
            redraw = true;
            continue;
        }
        if (m.ax() & 0x10) {
            // rom: L_341A3B uses `ss:[si]`, not DS.
            menu_move(m, true, last, m.ss());
            menu_sound(m, 0x92);
        } else if (m.ax() & 0x40) {
            // rom: L_341A46 uses `ss:[si]`, not DS.
            menu_move(m, false, last, m.ss());
            menu_sound(m, 0x92);
        }
        m.di() = 0x1A71;
        draw_cursor();
        menu_present(m);
    }
}

// rom: 0x34198B (near)
// Select one of seven party menu categories, returning after cancel or a submenu requests exit.
void select_party_menu_category(Machine &m) {
    select_party_menu_category_core(m);
    m.ret();
}
// Shared body of ROM 0x34229A
// Accept an available direction slot and run its field transition. CF rejects unavailable selections.
void accept_direction_choice_core(Machine &m) {
    m.set_al(10);
    read_menu_progress_nibble_core(m);
    // Each rejected path exposes the final CMP flags, followed by STC.
    menu_compare(m, m.al(), 0, true);
    if (m.al()) {
        menu_compare(m, m.al(), 5, true);
        if (m.al() < 5)
            menu_compare(m, m.al(), m.rd8(m.ds(), kTabActionIndex), true);
    }
    if (m.al() == 0 || m.al() >= 5 || m.al() <= m.rd8(m.ds(), kTabActionIndex)) {
        m.flags() |= emu::CF;
        return;
    }
    m.set_al(m.rd8(m.ds(), kTabActionIndex));
    m.set_al(uint8_t(m.al() + 1));
    m.wr8(m.ds(), kTabExitRequested, m.al());
    m.set_ah(0);
    m.set_al(uint8_t(m.al() - 1));
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        choose_tower_destination_core(m);
        m.cs() = caller_cs;
    }
    m.flags() &= ~emu::CF;
    return;
}

// rom: 0x34229A (near)
void accept_direction_choice(Machine &m) {
    accept_direction_choice_core(m);
    m.ret();
}
// Shared body of ROM 0x3421E3
// Select a two-by-two direction destination with cancel, confirm and wrapped cursor movement.
void select_direction_destination_core(Machine &m) {
    draw_direction_choices_core(m);
    m.wr8(m.ds(), kTabActionIndex, 0);
    for (;;) {
        menu_cursor_display(m, 0x22C2);
        m.call_far(0x4000, 0x5c);
        m.ax() = m.rd16(m.ds(), kKeysRepeat);
        m.dx() = uint16_t((m.dx() & 0xFF00) | 3);
        m.si() = kTabActionIndex;
        if (m.ax() & 8) {
            hide_secondary_menu_cursor_core(m);
            menu_sound(m, 0x94);
            break;
        }
        if (m.ax() & 4) {
            accept_direction_choice_core(m);
            if (m.flags() & emu::CF) {
                menu_sound(m, 0x2A);
                continue;
            }
            hide_secondary_menu_cursor_core(m);
            menu_sound(m, 0x93);
            break;
        }
        uint8_t toggle = (m.ax() & 0xA0) ? 1 : (m.ax() & 0x50) ? 2 : 0;
        if (toggle) {
            m.set_al(uint8_t(m.rd8(m.ds(), m.si()) ^ toggle));
            m.wr8(m.ds(), m.si(), m.al());
            menu_sound(m, 0x92);
        }
    }
    m.ax() = 1;
    m.bx() = 0x070B;
    m.cx() = 0x0608;
    m.dx() = 0x1FF;
    fill_screen_rect_core(m);
    return;
}

// rom: 0x3421E3 (near)
// Select a two-by-two direction destination with cancel, confirm and wrapped cursor movement.
void select_direction_destination(Machine &m) {
    select_direction_destination_core(m);
    m.ret();
}
// Shared body of ROM 0x3420E7
// Choose a class/gender action for the current member; the special direction class uses its own panel.
void select_class_action_core(Machine &m) {
    m.set_al(m.rd8(m.ds(), m.si()));
    m.wr8(m.ds(), kTabActionIndex, m.al());
    show_secondary_menu_cursor_core(m);
    if (!m.rd8(m.ss(), kTabListColumn) && m.al() == 8) {
        hooks::EventScope presentation(m, hooks::Event::ItemDialog, {5, 0});
        select_direction_destination_core(m);
        hide_secondary_menu_cursor_core(m);
        return;
    }
    draw_menu_selection_description_core(m);
    for (;;) {
        menu_cursor_display(m, 0x28E4);
        m.call_far(0x4000, 0x5c);
        m.ax() = m.rd16(m.ds(), kKeysRepeat);
        uint8_t maximum = m.rd8(m.ds(), kTabListColumn) ? 8 : 9;
        m.dx() = uint16_t((m.dx() & 0xFF00) | maximum);
        m.si() = maximum == 8 ? kTabGenderSelectIndex : kTabClassSelectIndex;
        if (m.ax() & 8) {
            hide_secondary_menu_cursor_core(m);
            menu_sound(m, 0x94);
            break;
        }
        if (m.ax() & 4) {
            menu_sound(m, 0x93);
            hooks::emit(hooks::Event::InventoryMenu, m, {0});
            apply_inventory_slot_choice_core(m);
            hide_secondary_menu_cursor_core(m);
            break;
        }
        if (m.ax() & 0x40) {
            menu_move(m, false, maximum);
            menu_sound(m, 0x92);
            draw_menu_selection_description_core(m);
        } else if (m.ax() & 0x10) {
            menu_move(m, true, maximum);
            menu_sound(m, 0x92);
            draw_menu_selection_description_core(m);
        }
    }
    return;
}

// rom: 0x3420E7 (near)
// Choose a class/gender action for the current member; the special direction class uses its own panel.
void select_class_action(Machine &m) {
    select_class_action_core(m);
    m.ret();
}
// Shared body of ROM 0x3464B5
// Draw the save completion message and wait until held keys equal the confirm key exactly.
void wait_save_confirmation_core(Machine &m) {
    UiSceneScope ui_scope(m, hooks::Event::SaveMenu, 5);
    menu_save(m);
    m.ax() = 0;
    m.ds() = 0;
    m.ax() = 0x1C;
    m.bx() = 0xE0;
    m.dx() = 0x3FFB;
    set_sprite_16x16_core(m);
    dma_sprite_table_core(m);
    m.ax() = 0;
    m.bx() = 1;
    m.cx() = 0x031C;
    draw_window_core(m);
    m.ax() = 1;
    m.bx() = 1;
    m.cx() = 0x021C;
    m.dx() = 0x1FF;
    fill_screen_rect_core(m);
    {
        const TextPtr t = text(m, TextKey::wait_save_confirmation_1);
        m.ds() = t.segment;
        m.ax() = 1;
        m.bx() = 2;
        m.si() = t.offset;
        draw_text_core(m);
        text_done(m, TextKey::wait_save_confirmation_1);
    }
    {
        const TextPtr t = text(m, TextKey::wait_save_confirmation_2);
        m.ds() = t.segment;
        m.ax() = 1;
        m.bx() = 0x102;
        m.si() = t.offset;
        draw_text_core(m);
        text_done(m, TextKey::wait_save_confirmation_2);
    }
    do {
        m.call_far(0x4000, 9);
        m.call_far(0x4000, 0x5c);
        m.ax() = uint16_t(hooks::filter(hooks::Value::SaveCompleteKeys, m, m.rd16(m.ss(), kKeysPressed)));
    } while (m.ax() != 4);
    menu_sound(m, 0x93);
    menu_restore(m);
    return;
}

// rom: 0x3464B5 (near)
// Draw the save completion message and wait until held keys equal the confirm key exactly.
void wait_save_confirmation(Machine &m) {
    wait_save_confirmation_core(m);
    m.ret();
}
// Shared body of ROM 0x34133A
// Back up the field graphics, open a selected party panel or its category menu, then return exit status AL.
void open_party_menu_core(Machine &m) {
    menu_save(m);
    m.ds() = m.ss();
    for (uint16_t off :
         {uint16_t(kTabExitRequested), uint16_t(kTabActionResult), uint16_t(kPartyMenuEntryContext)})
        m.wr8(m.ss(), off, 0);
    m.wr16(m.ss(), kLineScrollTable, m.ax());
    bool valid = true;
    if (m.ax()) {
        test_menu_categories_core(m);
        valid = (m.flags() & emu::CF) != 0;
    } else
        m.wr8(m.ss(), kPartyMenuEntryContext, uint8_t(m.bx()));
    if (valid) {
        // Three DMA backups: sprites, palette buffers and field screen maps.
        for (int i = 0; i < 3; ++i) {
            m.ax() = 0;
            menu_out16(m, 0x42, 0);
            m.ax() = i == 0 ? kSpriteBuffer : i == 1 ? kPaletteBuffer : kScreenMap2;
            menu_out16(m, 0x40, m.ax());
            m.ax() = i == 0 ? kSpriteBackup : i == 1 ? kPaletteTargetBuffer : kMenuScreenBackup;
            menu_out16(m, 0x44, m.ax());
            m.ax() = i == 2 ? 0x800 : 0x200;
            menu_out16(m, 0x46, m.ax());
            m.set_al(0x80);
            m.out8(0x48, m.al());
        }
        m.ax() = uint16_t(m.in8(0x10) | (m.in8(0x11) << 8));
        m.set_al(uint8_t(m.al() - 8));
        m.bx() = 0;
        m.ax() &= 0xF0F0;
        m.bx() = uint16_t(m.ah() << 3);
        m.set_al(uint8_t(m.al() >> 2));
        m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(uint8_t(m.bx()) + m.al()));
        m.ax() = 0x100;
        m.ds() = m.ax();
        m.ax() = 0x200;
        m.es() = m.ax();
        m.di() = 0;
        m.cx() = 0x1010;
        do {
            uint16_t row_bx = m.bx(), row_di = m.di();
            uint8_t columns = uint8_t(m.cx());
            do {
                for (uint16_t off : {uint16_t(0), uint16_t(2), uint16_t(0x40), uint16_t(0x42)}) {
                    m.ax() = m.rd16(m.ds(), uint16_t(m.bx() + off));
                    m.wr16(m.es(), uint16_t(m.di() + off), m.ax());
                }
                m.di() = uint16_t(m.di() + 4);
                m.bx() = uint16_t((m.bx() + 4) & 0xFFBF);
                --columns;
                m.cx() = uint16_t((m.cx() & 0xFF00) | columns);
            } while (columns);
            m.di() = uint16_t(row_di + 0x80);
            m.bx() = uint16_t((row_bx + 0x80) & 0xF7FF);
            m.cx() = uint16_t(m.cx() - 0x100 + 0x10);
        } while (uint8_t(m.cx() >> 8));
        m.call_far(0x4000, 9);
        m.ax() = 8;
        menu_out16(m, 0x10, m.ax());
        m.ax() = 0xFC08;
        menu_out16(m, 0x12, m.ax());
        m.set_al(1);
        m.out8(4, m.al());
        m.set_al(0x34);
        m.out8(7, m.al());
        for (uint16_t screen : {uint16_t(0), uint16_t(1)}) {
            m.ax() = screen;
            m.bx() = 0;
            m.cx() = 0x2020;
            m.dx() = 0x1FF;
            fill_screen_rect_core(m);
        }
        prepare_party_menu_entry_core(m);
        m.ax() = m.rd16(m.ss(), kLineScrollTable);
        bool choose_category = m.ax() != 0;
        if (m.ax() && !(m.ax() & 2)) {
            m.cx() = 4;
            m.si() = kPartyOrderLast;
            m.ax() = uint16_t(m.ax() << 4);
            do {
                bool bit = (m.ax() & 0x8000) != 0;
                m.ax() = uint16_t(m.ax() << 1);
                if (bit && m.rd8(m.ss(), m.si()) != 0 && m.rd8(m.ss(), m.si()) != 9) {
                    choose_category = false;
                    break;
                }
                --m.si();
                --m.cx();
            } while (m.cx());
        }
        if (choose_category) {
            m.set_al(0);
            m.wr8(m.ss(), kMenuDescriptionMode, 0);
            select_party_menu_category_core(m);
        } else {
            if (!m.rd16(m.ss(), kLineScrollTable)) {
                m.si() = kPartyMenuEntryContext;
                m.wr8(m.ss(), m.si(), uint8_t(m.rd8(m.ss(), m.si()) + 1));
            }
            m.set_al(m.rd8(m.ss(), m.si()));
            m.wr8(m.ss(), kMenuDescriptionMode, m.al());
            m.ax() = uint8_t(m.al() - 1);
            m.ax() = uint16_t(m.ax() << 1);
            m.bx() = m.ax();
            m.si() = m.rd16(m.cs(), uint16_t(m.bx() + 0x14AF));
            tab_call(m, m.si());
        }
    }
    menu_restore(m);
    m.set_ah(0);
    m.set_al(m.rd8(m.ss(), kTabActionResult));
    return;
}

// rom: 0x34133A
// Back up the field graphics, open a selected party panel or its category menu, then return exit status AL.
void open_party_menu(Machine &m) {
    open_party_menu_core(m);
    m.retf();
}
// Shared body of ROM 0x346102
// Confirm or cancel saving to the selected slot.
void confirm_save_slot_core(Machine &m) {
    UiSceneScope ui_scope(m, hooks::Event::SaveMenu, 4);
    m.ax() = 0;
    m.ds() = 0;
    m.set_al(1);
    m.wr8(m.ds(), kTabGenderSelectIndex, 1);
    m.di() = 0x61BD;
    draw_gender_select_cursor_core(m);
    draw_save_confirmation_core(m);
    dma_sprite_table_core(m);
    m.call_far(0x4000, 9);
    dma_palettes_core(m);
    for (;;) {
        m.call_far(0x4000, 0x5c);
        m.ax() = m.rd16(m.ds(), kKeysRepeat);
        m.dx() = uint16_t((m.dx() & 0xFF00) | 1);
        m.si() = kTabGenderSelectIndex;
        if ((m.ax() & 8) || ((m.ax() & 4) && m.rd8(m.ss(), kTabGenderSelectIndex))) {
            menu_sound(m, 0x94);
            break;
        }
        if (m.ax() & 4) {
            menu_sound(m, 0x11);
            if (m.rd8(m.ss(), kPartyMenuEntryContext))
                m.wr8(m.ss(), kSaveFromPartyMenu, 0xFF);
            m.set_al(
                save_slot_for_row(m, m.rd8(m.ss(), kTabClassSelectIndex))); // Save Slots 9: the row's slot
            write_save_slot_core(m);
            redraw_save_panels_core(m);
            draw_selected_save_party_core(m);
            dma_palettes_core(m);
            wait_save_confirmation_core(m);
            m.flags() &= ~emu::CF;
            break;
        }
        if (m.ax() & 0x80) {
            // rom: L_34619B uses `ss:[si]`, not DS.
            menu_move(m, true, 1, m.ss());
            menu_sound(m, 0x92);
        } else if (m.ax() & 0x20) {
            // rom: L_3461A6 uses `ss:[si]`, not DS.
            menu_move(m, false, 1, m.ss());
            menu_sound(m, 0x92);
        }
        m.di() = 0x61BD;
        draw_gender_select_cursor_core(m);
        menu_present(m);
    }
    return;
}

// rom: 0x346102 (near)
// Confirm or cancel saving to the selected slot.
void confirm_save_slot(Machine &m) {
    confirm_save_slot_core(m);
    m.ret();
}

// Shared body of ROM 0x34B35C
// Four-character name editor: wrapped character grid, three alphabets, backspace and early confirmation.
void edit_creation_name_core(Machine &m) {
    auto *name_session = Session::find(m.wsc());
    PlayerNames *host_names =
        name_session && name_session->player_names().enabled ? &name_session->player_names() : nullptr;
    if (host_names)
        host_names->draft.clear();
    menu_save(m);
    m.ax() = 0;
    m.ds() = 0;
    m.ax() = 1;
    m.bx() = 0;
    m.cx() = 0x2020;
    m.dx() = 0x1FF;
    fill_screen_rect_core(m);
    m.ax() = 0x4000;
    m.ds() = m.ax();
    m.ax() = 0x1F3;
    m.cx() = 1;
    m.si() = 0xC7FB;
    copy_tiles_core(m);
    m.ax() = 0;
    m.ds() = 0;
    m.es() = 0;
    for (uint16_t off : {uint16_t(kTabSourceColumn), uint16_t(kTabItemBuffer), uint16_t(kTabClassSelectIndex),
                         uint16_t(kTabGenderSelectIndex)})
        m.wr8(m.ss(), off, 0);
    for (uint16_t off = kNameText; off <= kTabTransferState; ++off)
        m.wr8(m.ss(), off, 0xD9);
    m.wr8(m.ss(), kTabTransferCount, 0xF3);
    draw_class_gender_icon_cursor_core(m);
    draw_name_entry_screen_core(m);
    draw_name_entry_alphabet_core(m);
    draw_name_entry_caret_core(m);
    hooks::emit(hooks::Event::NameEntry, m, {1});
    const LeaveScope scope{m, hooks::Event::NameEntry};
    menu_present(m);
    for (;;) {
        m.call_far(0x4000, 0x5c);
        m.ax() = uint16_t(hooks::filter(hooks::Value::NameEntryKeys, m, m.rd16(m.ss(), kKeysRepeat)));
        m.dx() = 0x0908;
        m.si() = kTabClassSelectIndex;
        if (m.ax() & 2) {
            m.ax() = 0x93;
            trigger_sound_effect_core(
                m); // Preserve the sound helper's arithmetic flags, read by this caller.
            m.ds() = m.ss();
            m.es() = m.ss();
            m.si() = kNameText;
            m.di() = (kSaveScratch + 0x1);
            // ADD DI,1 is the last arithmetic instruction on the early-confirm path.
            m.flags() = uint16_t((m.flags() & ~(emu::CF | emu::PF | emu::AF | emu::ZF | emu::SF | emu::OF)) |
                                 emu::SF | emu::PF);

            m.cx() = 4;
            m.flags() &= ~emu::DF;
            do {
                m.wr8(m.es(), m.di(), m.rd8(m.ds(), m.si()));
                ++m.si();
                ++m.di();
            } while (--m.cx());
            m.flags() &= ~emu::CF;
            break;
        }
        if (m.ax() & 8) {
            if (!m.rd8(m.ss(), kTabItemBuffer) && (m.rd16(m.ss(), kKeysPressed) & 8)) {
                menu_test(m, m.rd16(m.ss(), kKeysPressed) & 8);
                m.ax() = 0x94;
                trigger_sound_effect_core(m);
                m.flags() |= emu::CF;
                break;
            }
            if (m.rd8(m.ss(), kTabItemBuffer)) {
                m.wr8(m.ss(), kTabItemBuffer, uint8_t(m.rd8(m.ss(), kTabItemBuffer) - 1));
                m.di() = kNameText;
                m.bx() = m.rd8(m.ss(), kTabItemBuffer);
                m.di() = uint16_t(m.di() + m.bx());
                m.set_al(0xD9);
                if (!host_names || m.bx() < 4)
                    m.wr8(m.ss(), m.di(), m.al());
                if (host_names && !host_names->draft.empty())
                    host_names->draft.pop_back();
            }
            draw_name_entry_caret_core(m);
            menu_sound(m, 0x94);
        } else if (m.ax() & 4) {
            if (host_names ? m.rd8(m.ss(), kTabItemBuffer) < kPlayerNameGlyphs
                           : m.rd8(m.ss(), kTabItemBuffer) != 4) {
                m.ax() = m.rd8(m.ss(), kTabSourceColumn);
                const TextPtr sheet = m.al() < 3 // D_34B7B2[page]: 9 x 10 characters
                                          ? text(m, name_entry_sheet(m.al()))
                                          : TextPtr{m.cs(), m.rd16(m.cs(), uint16_t(0xB7B2 + m.al() * 2))};
                m.ax() = uint16_t(m.ax() << 1);
                m.si() = sheet.offset;
                m.set_al(m.rd8(m.ss(), kTabGenderSelectIndex));
                m.dx() = uint16_t((m.dx() & 0xFF00) | 10);
                m.ax() = uint16_t(m.al() * 10);
                m.set_al(uint8_t(m.al() + m.rd8(m.ss(), kTabClassSelectIndex)));
                m.si() = uint16_t(m.si() + m.ax());
                m.set_al(m.rd8(sheet.segment, m.si()));
                const auto glyph = uint32_t(hooks::filter(hooks::Value::NameEntryGlyph, m, m.al()));
                m.set_al(uint8_t(glyph));
                m.di() = kNameText;
                m.bx() = m.rd8(m.ss(), kTabItemBuffer);
                m.di() = uint16_t(m.di() + m.bx());
                if (!host_names || m.bx() < 4)
                    m.wr8(m.ss(), m.di(), m.al());
                if (host_names)
                    host_names->draft.push_back({m.al(), char32_t(glyph >> 8)});
                m.wr8(m.ss(), kTabItemBuffer, uint8_t(m.rd8(m.ss(), kTabItemBuffer) + 1));
            }
            draw_name_entry_caret_core(m);
            menu_sound(m, 0x93);
        } else if (m.ax() & 0x50) {
            ++m.si();
            menu_move(m, (m.ax() & 0x10) != 0, 8, m.ss());
            menu_sound(m, 0x92);
        } else if (m.ax() & 0xA0) {
            menu_move(m, (m.ax() & 0x80) != 0, 9, m.ss());
            menu_sound(m, 0x92);
        } else if (m.ax() & 0xA00) {
            m.si() = kTabSourceColumn;
            menu_move(m, (m.ax() & 0x800) != 0, 2, m.ss());
            draw_name_entry_alphabet_core(m);
            menu_sound(m, 0x92);
        }
        draw_class_gender_icon_cursor_core(m);
        menu_present(m);
    }
    menu_restore(m);
    return;
}

// rom: 0x34B35C
void edit_creation_name(Machine &m) {
    edit_creation_name_core(m);
    m.retf();
}
// Shared body of ROM 0x34AD05
// Choose a new member's race/template, edit its name, and copy the resulting record into the party.
void new_game_setup_core(Machine &m) {
    menu_save(m);
    m.wr8(m.ss(), kTabListColumn, m.al());
    m.wr8(m.ss(), kTabEquipmentMember, uint8_t(m.bx()));
    m.ax() = 0;
    m.ds() = 0;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        fade_palettes_16_core(m);
        m.cs() = caller_cs;
    }
    prepare_field_menu_display_core(m);
    m.ax() = 7;
    menu_out16(m, 0, m.ax());
    m.set_al(0x32);
    m.out8(7, m.al());
    m.set_al(1);
    m.out8(4, m.al());
    m.ax() = 0x9000;
    m.ds() = m.ax();
    m.ax() = 0x100;
    m.cx() = 0x100;
    m.si() = 0x80;
    copy_tiles_core(m);
    m.ax() = 15;
    m.cx() = 1;
    m.si() = 0;
    m.di() = kPaletteBuffer;
    copy_palettes_core(m);
    m.ax() = 0x9000;
    m.ds() = m.ax();
    m.ax() = 0x80;
    m.cx() = 0x20;
    m.si() = 0x2480;
    copy_tiles_core(m);
    m.ax() = 0xA0;
    m.cx() = 0x2C;
    m.si() = 0x2AC0;
    copy_tiles_core(m);
    m.ax() = 0;
    m.ds() = 0;
    m.es() = 0;
    m.wr8(m.ss(), kRaceSelectIndex, 0);
    draw_race_icon_cursor_core(m);
    draw_creation_type_screen_core(m);
    draw_creation_type_preview_core(m);
    hooks::emit(hooks::Event::CreationMenu, m, {1});
    const LeaveScope scope{m, hooks::Event::CreationMenu};
    menu_present(m);
    for (;;) {
        m.call_far(0x4000, 0x5c);
        m.ax() = uint16_t(hooks::filter(hooks::Value::CreationKeys, m, m.rd16(m.ss(), kKeysRepeat)));
        m.dx() = uint16_t((m.dx() & 0xFF00) | 7);
        m.si() = kRaceSelectIndex;
        if (m.ax() & 8) {
            menu_sound(m, 0x94);
            break;
        }
        if (m.ax() & 4) {
            menu_sound(m, 0x93);
            hooks::emit(hooks::Event::CreationMenu, m, {0});
            edit_creation_name_core(m);
            hooks::emit(hooks::Event::CreationMenu, m, {1}); // its screen is back (menu_restore)
            if (m.flags() & emu::CF) {
                m.ax() = 1;
                m.bx() = 0;
                m.cx() = 0x2020;
                m.dx() = 0x1FF;
                fill_screen_rect_core(m);
                draw_creation_type_screen_core(m);
                draw_creation_type_preview_core(m);
            } else {
                m.ds() = m.ss();
                m.es() = m.ss();
                m.si() = kSaveScratch;
                m.di() = kGameState;
                m.set_al(m.rd8(m.ss(), kTabListColumn));
                m.cx() = 31;
                m.ax() = uint16_t(m.al() * 31);
                m.di() = uint16_t(m.di() + m.ax());
                m.flags() &= ~emu::DF;
                do {
                    m.wr8(m.es(), m.di(), m.rd8(m.ds(), m.si()));
                    ++m.si();
                    ++m.di();
                } while (--m.cx());
                if (auto *s = Session::find(m.wsc()); s && s->player_names().enabled)
                    s->player_names().accept(m.rd8(m.ss(), kTabListColumn));
                m.set_al(m.rd8(m.ss(), kTabListColumn));
                clear_progress_history_core(m);
                m.set_al(m.rd8(m.ss(), kTabListColumn));
                m.bx() = uint16_t((m.bx() & 0xFF00) | m.al());
                m.dx() = uint16_t((m.dx() & 0xFF00) | 31);
                m.ax() = uint16_t(m.al() * 31 + kGameState);
                m.si() = m.ax();
                m.set_al(m.rd8(m.ss(), uint16_t(m.si() + 5)));
                m.set_ah(0xFF);
                append_progress_history_core(m);
                break;
            }
        } else if (m.ax() & 0x50) {
            // rom: L_34AE54 uses `ss:[si]`, not DS (DS may still hold whatever
            // edit_creation_name_core's internal redraw left it as).
            menu_move(m, (m.ax() & 0x10) != 0, 7, m.ss());
            draw_creation_type_preview_core(m);
            menu_sound(m, 0x92);
        } else if (m.rd8(m.ss(), kTabEquipmentMember) >= 6 && (m.ax() & 0xA0)) {
            m.si() = kTabEquipmentMember;
            uint8_t monster_type = m.rd8(m.ss(), m.si());
            if (m.ax() & 0x80) {
                monster_type = uint8_t(monster_type - 1);
                if (monster_type <= 6)
                    monster_type = 6;
            } else {
                monster_type = uint8_t(monster_type + 1);
                if (monster_type > 30)
                    monster_type = 30;
            }
            m.set_al(monster_type);
            m.wr8(m.ss(), m.si(), monster_type);
            draw_creation_type_screen_core(m);
            draw_creation_type_preview_core(m);
            menu_sound(m, 0x92);
        }
        draw_race_icon_cursor_core(m);
        menu_present(m);
    }
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        fade_palettes_16_core(m);
        m.cs() = caller_cs;
    }
    restore_field_menu_display_core(m);
    menu_restore(m);
    return;
}

// rom: 0x34AD05
void new_game_setup(Machine &m) {
    new_game_setup_core(m);
    m.retf();
}

namespace {
// Exact subtraction flags for menu helpers whose caller branches on the result.
void menu_compare(Machine &m, uint16_t left, uint16_t right, bool byte = false) {
    unsigned mask = byte ? 0xFF : 0xFFFF, sign = byte ? 0x80 : 0x8000;
    unsigned result = (left - right) & mask;
    uint16_t flags = m.flags() & ~(emu::CF | emu::PF | emu::AF | emu::ZF | emu::SF | emu::OF);
    if (left < right)
        flags |= emu::CF;
    if (!result)
        flags |= emu::ZF;
    if (result & sign)
        flags |= emu::SF;
    if ((std::popcount(uint8_t(result)) & 1) == 0)
        flags |= emu::PF;
    if ((left ^ right ^ result) & 0x10)
        flags |= emu::AF;
    if (((left ^ right) & (left ^ result) & sign))
        flags |= emu::OF;
    m.flags() = flags;
}
void menu_test(Machine &m, uint16_t result) {
    menu_compare(m, result, 0);
    m.flags() &= ~(emu::CF | emu::OF | emu::AF);
}
void wait_menu_key(Machine &m) {
    do {
        m.call_far(0x4000, 9);
        m.call_far(0x4000, 0x5c);
        m.ax() = m.rd16(m.ss(), kKeysPressed);
        menu_test(m, m.ax());
    } while (!m.ax());
}
void item_menu_frame(Machine &m, uint16_t cursor, uint8_t sprites = 0x18) {
    m.set_al(0);
    m.out8(5, 0);
    m.set_al(sprites);
    m.out8(6, m.al());
    if (cursor)
        switch (cursor) {
        case 0x22c2:
            draw_direction_choice_cursor_core(m);
            break;
        case 0x28e4:
            draw_class_or_gender_cursor_core(m);
            break;
        case 0x3688:
            draw_equipment_refill_cursor_core(m);
            break;
        case 0x2941:
            draw_refill_target_cursor_core(m);
            break;
        case 0x2978:
            draw_item_target_cursor_core(m);
            break;
        case 0x36e2:
            draw_equipment_action_cursor_core(m);
            break;
        case 0x4f15:
            draw_class_menu_cursor_core(m);
            break;
        default:
            break;
        }
    dma_sprite_table_core(m);
    m.call_far(0x4000, 9);
    dma_palettes_core(m);
    m.ax() = 7;
    menu_out16(m, 0, m.ax());
    m.ax() = 8;
    menu_out16(m, 0x10, m.ax());
    m.ax() = 0xFC08;
    menu_out16(m, 0x12, m.ax());
    m.set_al(0x32);
    m.out8(7, m.al());
    m.call_far(0x4000, 0x5c);
    m.ax() = m.rd16(m.ds(), kKeysRepeat);
}
uint16_t equipped_item_base(Machine &m) {
    m.ax() = m.rd8(m.ss(), kTabEquipmentMember);
    m.dx() = uint16_t((m.dx() & 0xFF00) | 31);
    m.ax() = uint16_t(m.al() * 31);
    return uint16_t(kMember0Equipment + m.ax());
}
void item_refill_selector(Machine &m, bool equipment) {
    hooks::EventScope presentation(m, hooks::Event::ItemDialog, {2, equipment ? 1 : 0});
    show_refill_menu_cursor_core(m);
    if (equipment) {
        draw_refill_inventory_core(m);
        m.wr8(m.ds(), kTabListColumn, 1);
    }
    draw_refill_instructions_core(m);
    for (;;) {
        item_menu_frame(m, equipment ? 0x3688 : 0x2941, equipment ? 0x1C : 0x18);
        m.dx() = uint16_t((m.dx() & 0xFF00) | 7);
        m.si() = (equipment && m.rd8(m.ds(), kTabListColumn)) ? kTabGenderSelectIndex : kTabClassSelectIndex;
        if (m.ax() & 8) {
            hide_secondary_menu_cursor_core(m);
            menu_sound(m, equipment ? 0x92 : 0x94);
            break;
        }
        if (m.ax() & 4) {
            menu_sound(m, 0x93);
            if (equipment)
                refill_selected_equipment_item_core(m);
            else
                refill_inventory_item_core(m);
            hide_secondary_menu_cursor_core(m);
            break;
        }
        if (m.ax() & 0x50) {
            // L_3424EF / L_3432FF test down (0x40) before up (0x10): with both held, down wins
            menu_move(m, (m.ax() & 0x40) == 0, 7);
            menu_sound(m, 0x92);
        }
    }
    if (equipment)
        m.wr8(m.ds(), kTabListColumn, 0);
}
void item_destination_selector(Machine &m, bool equipment) {
    hooks::EventScope presentation(m, hooks::Event::ItemDialog, {3, equipment ? 1 : 0});
    m.set_al(m.rd8(m.ds(), kCurrentFloor));
    m.wr8(m.ds(), kTabActionResult, m.al());
    draw_tower_destination_entry_core(m);
    draw_tower_instructions_core(m);
    hide_primary_menu_cursor_core(m);
    for (;;) {
        item_menu_frame(m, 0, equipment ? 0x1C : 0x18);
        m.dx() = uint16_t((m.dx() & 0xFF00) | m.rd8(m.ds(), kTowerCounter));
        m.si() = kTabActionResult;
        if (m.ax() & 8) {
            hide_secondary_menu_cursor_core(m);
            menu_sound(m, 0x94);
            break;
        }
        if (m.ax() & 4) {
            menu_sound(m, 0x93);
            if (equipment)
                use_equipment_destination_core(m);
            else
                use_inventory_destination_core(m);
            hide_secondary_menu_cursor_core(m);
            break;
        }
        if (m.ax() & 0x50) {
            uint8_t destination_row = m.rd8(m.ds(), m.si());
            if (m.ax() & 0x10) {
                destination_row = uint8_t(destination_row + 1);
                if (destination_row > uint8_t(m.dx()))
                    destination_row = uint8_t(m.dx());
            } else {
                destination_row = uint8_t(destination_row - 1);
                if (!destination_row)
                    destination_row = 1;
            }
            m.set_al(destination_row);
            m.wr8(m.ds(), m.si(), destination_row);
            m.ax() = 1;
            m.bx() = 0x080F;
            draw_tower_destination_number_core(m);
            menu_sound(m, 0x92);
        }
    }
    m.ax() = 1;
    m.bx() = 0x0808;
    m.cx() = 0x030D;
    m.dx() = 0x1FF;
    fill_screen_rect_core(m);
}
void item_target_selector(Machine &m, bool equipment) {
    hooks::EventScope presentation(m, hooks::Event::ItemDialog, {1, equipment ? 1 : 0});
    draw_item_targets_core(m);
    for (;;) {
        item_menu_frame(m, 0x2978, equipment ? 0x1C : 0x18);
        m.dx() = uint16_t((m.dx() & 0xFF00) | 3);
        m.si() = kTabThirdSelection;
        if (m.ax() & 8) {
            hide_secondary_menu_cursor_core(m);
            menu_sound(m, 0x94);
            break;
        }
        if (m.ax() & 4) {
            menu_sound(m, 0x93);
            if (equipment)
                use_equipment_item_on_actor_core(m);
            else
                use_inventory_item_on_actor_core(m);
            if (!(m.flags() & emu::CF))
                continue;
            hide_secondary_menu_cursor_core(m);
            break;
        }
        if (m.ax() & 0x50) {
            // L_342725 / L_34354B test down (0x40) before up (0x10): with both held, down wins
            bool up = (m.ax() & 0x40) == 0;
            menu_move(m, up, 3);
            (up ? select_previous_party_member_core : select_next_party_member_core)(m);
            menu_sound(m, 0x92);
        }
    }
}
} // namespace
// Shared body of ROM 0x342383
// Swap/discard different inventory slots, or dispatch an action when the source and destination coincide.
void apply_inventory_slot_choice_core(Machine &m) {
    bool special = m.rd8(m.ds(), kTabListColumn) != 0;
    m.si() = special ? kTabGenderSelectIndex : kTabClassSelectIndex;
    m.set_al(m.rd8(m.ds(), kTabActionIndex));
    uint8_t selected = m.rd8(m.ds(), m.si());
    if (m.al() == selected) {
        if (!special && m.al() == 9) {
            m.flags() |= emu::CF;
            return;
        }
        m.si() = special ? kKeyItems : kInventory;
        dispatch_inventory_item_action_core(m);
        return;
    }
    if (!special && (m.al() == 9 || selected == 9)) {
        if (m.al() != 9)
            m.set_al(m.rd8(m.ds(), kTabActionIndex));
        else
            m.set_al(selected);
        if (m.al() == 8) {
            m.flags() |= emu::CF;
            return;
        }
        m.si() = kInventory;
        erase_item_record_core(m);
    } else {
        m.set_al(selected);
        m.bx() = uint16_t((m.bx() & 0xFF00) | m.rd8(m.ds(), kTabActionIndex));
        if (!special && (m.al() == 8 || uint8_t(m.bx()) == 8)) {
            m.flags() |= emu::CF;
            return;
        }
        m.si() = special ? kKeyItems : kInventory;
        swap_item_records_core(m);
    }
    m.flags() &= ~emu::CF;
    return;
}

// rom: 0x342383 (near)
void apply_inventory_slot_choice(Machine &m) {
    apply_inventory_slot_choice_core(m);
    m.ret();
}
// Shared body of ROM 0x3423FD
// Dispatch an inventory item's field action or reject an unusable choice.
void dispatch_inventory_item_action_core(Machine &m) {
    m.set_ah(0);
    m.ax() = uint16_t(m.ax() << 1);
    m.si() = uint16_t(m.si() + m.ax());
    uint8_t uses = m.rd8(m.ss(), uint16_t(m.si() + 1));
    if (!uses || uses == 0xFF) {
        menu_compare(m, uses, uses == 0 ? 0 : 0xFF, true);
        m.flags() |= emu::CF;
        return;
    }
    m.set_al(m.rd8(m.ds(), m.si()));
    uint8_t id = m.al();
    uint16_t field = id == 0x11   ? 0xB9B9
                     : id == 0x12 ? 0xB9CC
                     : id == 0x13 ? 0xB9DE
                     : id == 0x15 ? 0xBA8E
                     : id == 0x16 ? 0xBA93
                     : id == 0x17 ? 0xBAFD
                                  : 0;
    if (field)
        menu_field_item(m, field);
    else if (id == 10)
        select_inventory_refill_core(m);
    else if (id == 11)
        select_inventory_destination_core(m);
    else if (id <= 16 || (id >= 0x7A && id <= 0x7D))
        select_inventory_item_target_core(m);
    else {
        m.ax() = 0;
        {
            const uint16_t native_caller_cs = m.cs();
            m.cs() = 0x4000;
            wait_unusable_item_message_core(m);
            m.cs() = native_caller_cs;
        }
    }
    m.flags() &= ~emu::CF;
    return;
}

// rom: 0x3423FD (near)
void dispatch_inventory_item_action(Machine &m) {
    dispatch_inventory_item_action_core(m);
    m.ret();
}
// Shared body of ROM 0x34248A
// Select an inventory item to refill and redraw its use count.
void select_inventory_refill_core(Machine &m) {
    item_refill_selector(m, false);
    return;
}

// rom: 0x34248A (near)
// Select an inventory item to refill and redraw its use count.
void select_inventory_refill(Machine &m) {
    select_inventory_refill_core(m);
    m.ret();
}
// Shared body of ROM 0x342576
// Select a clamped tower destination for the inventory travel item.
void select_inventory_destination_core(Machine &m) {
    item_destination_selector(m, false);
    return;
}

// rom: 0x342576 (near)
// Select a clamped tower destination for the inventory travel item.
void select_inventory_destination(Machine &m) {
    select_inventory_destination_core(m);
    m.ret();
}
// Shared body of ROM 0x34263B
// Mark travel-menu exit and pass the selected inventory record to the destination transition.
void use_inventory_destination_core(Machine &m) {
    m.wr8(m.ds(), kTabExitRequested, 11);
    m.si() = kInventory;
    m.ax() = uint16_t(m.rd8(m.ds(), kTabClassSelectIndex) * 2);
    m.si() = uint16_t(m.si() + m.ax());
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        use_destination_travel_item_core(m);
        m.cs() = caller_cs;
    }
    return;
}

// rom: 0x34263B (near)
void use_inventory_destination(Machine &m) {
    use_inventory_destination_core(m);
    m.ret();
}
// Shared body of ROM 0x3426B9
// Select a party member for an inventory item's effect, preserving the original repeated-use loop.
void select_inventory_item_target_core(Machine &m) {
    item_target_selector(m, false);
    return;
}

// rom: 0x3426B9 (near)
// Select a party member for an inventory item's effect, preserving the original repeated-use loop.
void select_inventory_item_target(Machine &m) {
    select_inventory_item_target_core(m);
    m.ret();
}
// Shared body of ROM 0x34281F
// Display the unusable-item message and wait for any newly pressed key; registers preserved.
void wait_unusable_item_message_core(Machine &m) {
    menu_save(m);
    m.ax() = 0;
    m.bx() = 0x0E09;
    m.cx() = 0x020C;
    draw_window_core(m);
    const TextPtr t = text(m, TextKey::wait_unusable_item_message_1);
    m.ds() = t.segment;
    m.ax() = 1;
    m.si() = t.offset;
    m.bx() = 0x0E0A;
    m.cx() = 10;
    draw_text_n_white_core(m);
    text_done(m, TextKey::wait_unusable_item_message_1);
    menu_sound(m, 0x2A);
    {
        hooks::EventScope presentation(m, hooks::Event::ItemMessage, {1});
        wait_menu_key(m);
    }
    m.ax() = 1;
    m.bx() = 0x0E09;
    m.cx() = 0x020B;
    m.dx() = 0x1FF;
    fill_screen_rect_core(m);
    menu_restore(m);
    return;
}

// rom: 0x34281F
// Display the unusable-item message and wait for any newly pressed key; registers preserved.
void wait_unusable_item_message(Machine &m) {
    wait_unusable_item_message_core(m);
    m.retf();
}
// Shared body of ROM 0x343286
// Select an inventory/equipment item for refill, restoring inventory mode afterward.
void select_equipment_refill_core(Machine &m) {
    item_refill_selector(m, true);
    return;
}

// rom: 0x343286 (near)
// Select an inventory/equipment item for refill, restoring inventory mode afterward.
void select_equipment_refill(Machine &m) {
    select_equipment_refill_core(m);
    m.ret();
}
// Shared body of ROM 0x3433FF
// Choose a tower destination for an equipped travel item.
void select_equipment_destination_core(Machine &m) {
    item_destination_selector(m, true);
    return;
}

// rom: 0x3433FF (near)
// Choose a tower destination for an equipped travel item.
void select_equipment_destination(Machine &m) {
    select_equipment_destination_core(m);
    m.ret();
}
// Shared body of ROM 0x3434C4
// Pass the selected actor's equipped travel item to the destination transition.
void use_equipment_destination_core(Machine &m) {
    m.wr8(m.ds(), kTabExitRequested, 11);
    m.si() = equipped_item_base(m);
    m.ax() = uint16_t(m.rd8(m.ds(), kTabActionIndex) * 2);
    m.si() = uint16_t(m.si() + m.ax());
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x5000;
        use_destination_travel_item_core(m);
        m.cs() = caller_cs;
    }
    return;
}

// rom: 0x3434C4 (near)
void use_equipment_destination(Machine &m) {
    use_equipment_destination_core(m);
    m.ret();
}
// Shared body of ROM 0x3434E7
// Select an actor target for an equipped item.
void select_equipment_item_target_core(Machine &m) {
    item_target_selector(m, true);
    return;
}

// rom: 0x3434E7 (near)
// Select an actor target for an equipped item.
void select_equipment_item_target(Machine &m) {
    select_equipment_item_target_core(m);
    m.ret();
}
// Shared body of ROM 0x3442A8
// Draw an equipment transfer error and wait for a newly pressed key, preserving DS.
void wait_equipment_transfer_error_core(Machine &m) {
    uint16_t ds = m.ds();
    const TextPtr t = text(m, TextKey::wait_equipment_transfer_error_1);
    m.ds() = t.segment;
    m.ax() = 1;
    m.bx() = 0x1002;
    m.si() = t.offset;
    draw_text_core(m);
    text_done(m, TextKey::wait_equipment_transfer_error_1);
    m.ds() = ds;
    menu_sound(m, 0x2A);
    hooks::EventScope presentation(m, hooks::Event::ItemMessage, {2});
    wait_menu_key(m);
    return;
}

// rom: 0x3442A8 (near)
// Draw an equipment transfer error and wait for a newly pressed key, preserving DS.
void wait_equipment_transfer_error(Machine &m) {
    wait_equipment_transfer_error_core(m);
    m.ret();
}
// Shared body of ROM 0x344C77
// Swap the selected party records and repair the dialogue leader/order references.
void swap_selected_party_members_core(Machine &m) {
    m.ax() = 0;
    m.ds() = 0;
    m.si() = kGameState;
    m.ax() = m.rd8(m.ds(), kTabActionIndex);
    m.dx() = uint16_t((m.dx() & 0xFF00) | 31);
    m.ax() = uint16_t(m.ax() * 31);
    m.si() = uint16_t(m.si() + m.ax());
    m.di() = kGameState;
    m.ax() = m.rd8(m.ds(), kTabClassSelectIndex);
    m.dx() = uint16_t((m.dx() & 0xFF00) | 31);
    m.ax() = uint16_t(m.ax() * 31);
    m.di() = uint16_t(m.di() + m.ax());
    menu_compare(m, m.si(), m.di());
    if (m.si() == m.di()) {
        m.flags() |= emu::CF;
        return;
    }
    validate_party_reorder_core(m);
    if (m.flags() & emu::CF) {
        return;
    }
    menu_push(m, m.si());
    menu_push(m, m.di());
    party_menu(m, 10); // the host order screen says the two are swapping (presentation only)
    animate_party_member_swap_core(m);
    m.di() = menu_pop(m);
    m.si() = menu_pop(m);
    swap_party_records_core(m);
    m.flags() &= ~emu::CF;
    return;
}

// rom: 0x344C77 (near)
void swap_selected_party_members(Machine &m) {
    swap_selected_party_members_core(m);
    m.ret();
}

namespace {
void refresh_equipment_rows(Machine &m) {
    uint16_t si = m.si();
    m.si() = equipped_item_base(m);
    m.ax() = 0;
    m.cx() = 8;
    m.bx() = 0x0703;
    draw_item_entry_rows_entry_core(m);
    m.si() = si;
}
// Apply the selected field item, spend a finite use, and report exhaustion in CF.
void use_item_on_actor(Machine &m, bool equipment) {
    // A new action replaces the previous native gain line.
    hooks::emit(hooks::Event::ItemMessage, m, {0});
    hooks::EventScope audio(m, hooks::Event::SoundContext, {int(hooks::AudioContext::FieldItem)});
    uint8_t source_list = m.rd8(m.ds(), kTabListColumn);
    menu_compare(m, source_list, 0, true);
    if (source_list) {
        m.flags() &= ~emu::CF;
        return;
    }
    if (equipment) {
        m.ax() = m.rd8(m.ss(), kTabEquipmentMember);
        m.dx() = uint16_t((m.dx() & 0xFF00) | 31);
        m.ax() = uint16_t(m.ax() * 31);
        m.si() = uint16_t(kGameState + m.ax());
        m.bp() = m.si();
        m.si() = uint16_t(m.si() + 15);
    } else
        m.si() = kInventory;
    m.ax() = uint16_t(m.rd8(m.ds(), kTabActionIndex) * 2);
    m.si() = uint16_t(m.si() + m.ax());
    m.set_al(m.rd8(m.ds(), kTabThirdSelection));
    uint16_t target = m.al() == 3   ? (kGameState + 0x5D)
                      : m.al() == 2 ? (kGameState + 0x3E)
                      : m.al() == 1 ? (kGameState + 0x1F)
                                    : kGameState;
    m.di() = target;
    if (!equipment)
        m.bp() = target;
    apply_selected_item_effect_core(m);
    if (m.flags() & emu::CF) {
        draw_item_error_tiles_core(m);
        if (equipment)
            refresh_equipment_rows(m);
        else {
            menu_push(m, m.si());
            draw_inventory_items_core(m);
            m.si() = menu_pop(m);
        }
        m.flags() &= ~emu::CF;
        return;
    }
    // FEh uses are unlimited; exhausting an ordinary item removes its record.
    const bool finite = m.rd8(m.ds(), uint16_t(m.si() + 1)) != 0xFE;
    const bool spend = finite && hooks::should(hooks::VB::SpendUse, m, true, {1, -1, m.rd8(m.ds(), m.si())});
    // A stat item (Essence of Power/Speed, HP200-600: ids 12-16) whose use Unbreakable Gear kept: back to the
    // item list after the one use, as when its last use is spent, instead of staying on the target picker.
    const uint8_t used_id = m.rd8(m.ds(), m.si());
    const bool kept_stat_item = finite && !spend && used_id >= 12 && used_id <= 16;
    if (spend) {
        uint16_t uses_address = uint16_t(m.si() + 1);
        uint8_t previous_uses = m.rd8(m.ds(), uses_address), remaining_uses = uint8_t(previous_uses - 1);
        m.wr8(m.ds(), uses_address, remaining_uses);
        if (equipment)
            refresh_equipment_rows(m);
        else {
            menu_push(m, m.si());
            draw_inventory_items_core(m);
            m.si() = menu_pop(m);
        }
        if (!m.rd8(m.ds(), uses_address)) {
            uint8_t item_id = m.rd8(m.ds(), m.si());
            if (item_id < 0x80 || (equipment && item_id >= 0xFB)) {
                m.set_al(item_id);
                m.wr16(m.ds(), m.si(), 0xFFFF);
            }
        }
    }
    menu_push(m, m.ax());
    menu_push(m, m.si());
    draw_item_targets_core(m);
    dma_sprite_table_core(m);
    dma_palettes_core(m);
    m.call_far(0x4000, 9);
    m.si() = menu_pop(m);
    m.ax() = menu_pop(m);
    menu_push(m, m.si());
    if (m.rd16(m.ss(), m.si()) == 0xFFFF || !m.rd8(m.ss(), uint16_t(m.si() + 1)) ||
        (m.al() >= 12 && m.al() <= 16) || kept_stat_item)
        wait_menu_key(m);
    m.si() = menu_pop(m);
    uint16_t item_record = m.rd16(m.ss(), m.si());
    menu_compare(m, item_record, 0xFFFF);
    bool exhausted = item_record == 0xFFFF;
    if (equipment && !exhausted) {
        uint8_t uses = m.rd8(m.ss(), uint16_t(m.si() + 1));
        menu_compare(m, uses, 0, true);
        exhausted = uses == 0;
    }
    if (exhausted || kept_stat_item)
        m.flags() |= emu::CF;
    else
        m.flags() &= ~emu::CF;
}
// Resolve the selected inventory/key-item/equipment slot to its two-byte item record.
uint16_t selected_item_pointer(Machine &m, uint16_t mode_off, uint16_t slot_off,
                               bool special_inventory = false) {
    uint16_t base;
    if (m.rd8(m.ss(), mode_off))
        base = (special_inventory && m.rd8(m.ss(), kTabItemBuffer)) ? kKeyItems : kInventory;
    else
        base = equipped_item_base(m);
    m.ax() = uint16_t(m.rd8(m.ds(), slot_off) * 2);
    return uint16_t(base + m.ax());
}
} // namespace
// Shared body of ROM 0x342761
// Apply an inventory item to the selected actor, consume ordinary charges and wait when the item expires.
void use_inventory_item_on_actor_core(Machine &m) {
    use_item_on_actor(m, false);
    return;
}

// rom: 0x342761 (near)
void use_inventory_item_on_actor(Machine &m) {
    use_inventory_item_on_actor_core(m);
    m.ret();
}
// Shared body of ROM 0x343586
// Apply an equipped item to the selected actor; refresh its rows and report exhausted/removed items in CF.
void use_equipment_item_on_actor_core(Machine &m) {
    use_item_on_actor(m, true);
    return;
}

// rom: 0x343586 (near)
void use_equipment_item_on_actor(Machine &m) {
    use_equipment_item_on_actor_core(m);
    m.ret();
}
// Shared body of ROM 0x343126
// Dispatch the selected item only when source and destination coincide and it has usable charges.
void accept_equipment_item_action_core(Machine &m) {
    m.ax() = 0;
    m.ds() = 0;
    m.si() = selected_item_pointer(m, kTabSourceColumn, kTabActionIndex);
    m.di() = selected_item_pointer(
        m, kTabListColumn, m.rd8(m.ds(), kTabListColumn) ? kTabGenderSelectIndex : kTabClassSelectIndex);
    menu_compare(m, m.si(), m.di());
    if (m.si() != m.di()) {
        m.flags() |= emu::CF;
        return;
    }
    uint8_t uses = m.rd8(m.ds(), uint16_t(m.si() + 1));
    menu_compare(m, uses, 0, true);
    if (uses) {
        menu_compare(m, uses, 0xFF, true);
        if (uses != 0xFF) {
            dispatch_equipment_item_action_core(m);
            m.flags() &= ~emu::CF;
            return;
        }
    }
    m.flags() |= emu::CF;
    return;
}

// rom: 0x343126 (near)
void accept_equipment_item_action(Machine &m) {
    accept_equipment_item_action_core(m);
    m.ret();
}
// Shared body of ROM 0x3431E2
// Dispatch field keys, refill, transfer, or target selection from the equipped item ID.
void dispatch_equipment_item_action_core(Machine &m) {
    m.set_al(m.rd8(m.ds(), m.si()));
    uint8_t id = m.al();
    uint16_t field = id == 0x11   ? 0xB9B9
                     : id == 0x12 ? 0xB9CC
                     : id == 0x13 ? 0xB9DE
                     : id == 0x15 ? 0xBA8E
                     : id == 0x16 ? 0xBA93
                     : id == 0x17 ? 0xBAFD
                                  : 0;
    if (field) {
        menu_field_item(m, field);
        if (id == 0x12 && !(m.flags() & emu::CF)) {
            m.wr8(m.ss(), m.si(), 0x12);
            m.wr8(m.ss(), uint16_t(m.si() + 1), 0xFE);
            m.di() = kTabItemBuffer;
            m.wr8(m.ss(), m.di(), 0xFF);
            m.wr8(m.ss(), uint16_t(m.di() + 1), 0xFF);
            replace_equipment_stats_core(m);
            m.wr16(m.ss(), m.si(), 0xFFFF);
        }
    } else if (id == 10)
        select_equipment_refill_core(m);
    else if (id == 11 || id == 0x86)
        select_equipment_destination_core(m);
    else if (id <= 16 || (id >= 0x7A && id <= 0x7D) || (id >= 0x80 && id <= 0x86))
        select_equipment_item_target_core(m);
    else {
        m.ax() = 0;
        {
            const uint16_t native_caller_cs = m.cs();
            m.cs() = 0x4000;
            wait_unusable_item_message_core(m);
            m.cs() = native_caller_cs;
        }
    }
    return;
}

// rom: 0x3431E2 (near)
// Dispatch equipment item effects, including the consumable tool that transfers through the item list.
void dispatch_equipment_item_action(Machine &m) {
    dispatch_equipment_item_action_core(m);
    m.ret();
}
// Shared body of ROM 0x34333A
// Refill a different eligible item to its table maximum and spend one refill-item use.
void refill_selected_equipment_item_core(Machine &m) {
    m.si() = equipped_item_base(m);
    m.ax() = uint16_t(m.rd8(m.ds(), kTabActionIndex) * 2);
    m.si() = uint16_t(m.si() + m.ax());
    m.di() = selected_item_pointer(
        m, kTabListColumn, m.rd8(m.ss(), kTabListColumn) ? kTabGenderSelectIndex : kTabClassSelectIndex);
    menu_compare(m, m.si(), m.di());
    bool rejected = m.si() == m.di();
    uint8_t id = m.rd8(m.ds(), m.di()), uses = m.rd8(m.ds(), uint16_t(m.di() + 1));
    if (!rejected) {
        menu_compare(m, id, 10, true);
        rejected = id == 10;
    }
    if (!rejected) {
        menu_compare(m, id, 0x80, true);
        rejected = id >= 0x80;
    }
    if (!rejected) {
        menu_compare(m, uses, 0xFF, true);
        rejected = uses == 0xFF;
    }
    if (rejected) {
        m.flags() |= emu::CF;
        return;
    }
    m.ax() = 0x9000;
    m.es() = m.ax();
    m.bx() = uint16_t(id * 2 + 0x41E0);
    m.bx() = m.rd16(m.es(), m.bx());
    m.set_al(table_at(m, Table::item_records, m.es(), uint16_t(m.bx() + 3)));
    m.wr8(m.ds(), uint16_t(m.di() + 1), m.al());
    if (hooks::should(hooks::VB::SpendUse, m, true, {2, -1, m.rd8(m.ds(), m.si())}))
        m.wr8(m.ds(), uint16_t(m.si() + 1), uint8_t(m.rd8(m.ds(), uint16_t(m.si() + 1)) - 1));
    refresh_equipment_rows(m);
    if (!m.rd8(m.ds(), uint16_t(m.si() + 1)))
        m.wr16(m.ds(), m.si(), 0xFFFF);
    if (m.rd8(m.ss(), kTabListColumn)) {
        draw_refill_inventory_core(m);
        wait_menu_key(m);
    }
    m.flags() &= ~emu::CF;
    return;
}

// rom: 0x34333A (near)
void refill_selected_equipment_item(Machine &m) {
    refill_selected_equipment_item_core(m);
    m.ret();
}
// Shared body of ROM 0x3440C4
void apply_equipment_transfer_choice_core(Machine &m) {
    m.ax() = 0;
    m.ds() = 0;
    m.si() = selected_item_pointer(m, kTabSourceColumn, kTabActionIndex, true);
    m.di() = selected_item_pointer(
        m, kTabListColumn, m.rd8(m.ss(), kTabListColumn) ? kTabGenderSelectIndex : kTabClassSelectIndex,
        true);
    menu_compare(m, m.si(), m.di());
    if (m.si() == m.di()) {
        auto_transfer_equipment_item_core(m);
        return;
        return;
    }
    validate_key_item_transfer_core(m);
    if (m.flags() & emu::CF) {
        m.ax() = 0x2A;
        trigger_sound_effect_core(m);
        m.flags() |= emu::CF;
        return;
        return;
    }
    prepare_equipment_transfer_core(m);
    if (m.flags() & emu::CF) {
        wait_equipment_transfer_error_core(m);
        m.flags() |= emu::CF;
        return;
        return;
    }
    swap_equipment_records_core(m);
    m.flags() &= ~emu::CF;
    return;
}

// rom: 0x3440C4 (near)
// Transfer or act on selected equipment/inventory records; reject invalid or incompatible moves in CF.
void apply_equipment_transfer_choice(Machine &m) {
    apply_equipment_transfer_choice_core(m);
    m.ret();
}
// Shared body of ROM 0x34417B
// Move an item into the first free compatible slot, or toggle lists for an empty record. CF rejects errors.
void auto_transfer_equipment_item_core(Machine &m) {
    uint8_t uses = m.rd8(m.ss(), uint16_t(m.si() + 1));
    menu_compare(m, uses, 0xFF, true);
    bool error = false;
    if (uses == 0xFF) {
        uint8_t locked = m.rd8(m.ss(), kNameText);
        menu_compare(m, locked, 0, true);
        if (locked)
            error = true;
        else {
            uint8_t mode = uint8_t(m.rd8(m.ss(), kTabListColumn) ^ 1);
            m.wr8(m.ss(), kTabListColumn, mode);
            menu_test(m, mode);
            if (mode && m.rd8(m.ss(), kTabItemBuffer) != 1) {
                uint8_t slot = m.rd8(m.ss(), kTabGenderSelectIndex);
                menu_compare(m, slot, 8, true);
                if (slot == 8) {
                    m.wr8(m.ss(), kTabGenderSelectIndex, 7);
                    menu_compare(m, 7, 0, true);
                }
            }
            m.flags() |= emu::CF;
            return;
        }
    } else if (m.rd8(m.ss(), kTabListColumn) != 1) {
        uint8_t id = m.rd8(m.ss(), m.si());
        bool special = (id >= 0x11 && id <= 0x1A) || id == 0x1E || id == 0x1F;
        m.wr8(m.ss(), kTabItemBuffer, special ? 1 : 0);
        m.di() = special ? kKeyItems : kInventory;
        m.cx() = special ? 9 : 8;
        menu_push(m, m.cx());
        menu_push(m, m.di());
        draw_equipment_edit_screen_core(m);
        m.di() = menu_pop(m);
        m.cx() = menu_pop(m);
        while (m.cx()) {
            uint8_t uses = m.rd8(m.ss(), uint16_t(m.di() + 1));
            menu_compare(m, uses, 0xFF, true);
            if (uses == 0xFF)
                break;
            m.di() = uint16_t(m.di() + 2);
            --m.cx();
        }
        error = !m.cx();
    } else {
        uint8_t id = m.rd8(m.ss(), m.di());
        bool allowed = !m.rd8(m.ss(), kTabItemBuffer) || id == 0x11 || id == 0x12 || id == 0x13;
        if (!allowed)
            error = true;
        else {
            m.ax() = m.rd8(m.ss(), kTabEquipmentMember);
            m.dx() = uint16_t((m.dx() & 0xFF00) | 31);
            m.ax() = uint16_t(m.ax() * 31);
            m.si() = uint16_t(kGameState + m.ax());
            uint8_t type = m.rd8(m.ss(), uint16_t(m.si() + 10));
            // Humans accept eight equipment slots; mutants accept only the last four, after innate abilities.
            if (type & 0x80) {
                m.si() = uint16_t(m.si() + 15);
                m.cx() = 8;
            } else if (type & 0x20) {
                m.si() = uint16_t(m.si() + 23);
                m.cx() = 4;
            } else
                error = true;
            if (!error) {
                while (m.cx()) {
                    uint8_t uses = m.rd8(m.ss(), uint16_t(m.si() + 1));
                    menu_compare(m, uses, 0xFF, true);
                    if (uses == 0xFF)
                        break;
                    m.si() = uint16_t(m.si() + 2);
                    --m.cx();
                }
                error = !m.cx();
            }
        }
    }
    if (error) {
        m.ax() = 0x2A;
        trigger_sound_effect_core(m);
        m.flags() |= emu::CF;
        return;
    }
    replace_equipment_stats_core(m);
    if (m.flags() & emu::CF) {
        wait_equipment_transfer_error_core(m);
        m.flags() |= emu::CF;
        return;
    }
    swap_equipment_records_core(m);
    m.flags() &= ~emu::CF;
    return;
}

// rom: 0x34417B (near)
void auto_transfer_equipment_item(Machine &m) {
    auto_transfer_equipment_item_core(m);
    m.ret();
}
// Shared body of ROM 0x343024
// Open an equipped-item action panel for the selected source slot and close it on cancel or confirm.
void select_equipment_item_action_core(Machine &m) {
    menu_push(m, m.si());
    draw_equipment_left_marker_core(m);
    draw_equipment_right_marker_core(m);
    m.si() = menu_pop(m);
    m.set_al(m.rd8(m.ds(), m.si()));
    m.wr8(m.ds(), kTabActionIndex, m.al());
    m.set_al(m.rd8(m.ds(), kTabListColumn));
    m.wr8(m.ds(), kTabSourceColumn, m.al());
    show_secondary_menu_cursor_core(m);
    if (m.al() == 8) {
        select_direction_destination_core(m);
        hide_secondary_menu_cursor_core(m);
        return;
    }
    draw_menu_selection_description_core(m);
    party_menu(m, 9); // the picked row waits for A or B (presentation only)
    for (;;) {
        item_menu_frame(m, 0x36E2, 0x1C);
        m.dx() = uint16_t((m.dx() & 0xFF00) | 7);
        m.si() = m.rd8(m.ds(), kTabListColumn) ? kTabGenderSelectIndex : kTabClassSelectIndex;
        if (m.ax() & 8) {
            hide_secondary_menu_cursor_core(m);
            menu_sound(m, 0x94);
            break;
        }
        if (m.ax() & 4) {
            menu_sound(m, 0x93);
            party_menu(m, 0); // its dialogs and messages are their own screens
            accept_equipment_item_action_core(m);
            hide_secondary_menu_cursor_core(m);
            break;
        }
    }
    return;
}

// rom: 0x343024 (near)
// Open an equipped-item action panel for the selected source slot and close it on cancel or confirm.
void select_equipment_item_action(Machine &m) {
    select_equipment_item_action_core(m);
    m.ret();
}
// Shared body of ROM 0x342E5B
// Equipment item-use menu: select member/list/slot, dispatch its action, or leave on cancel/field transition.
void run_equipment_item_menu_core(Machine &m) {
    UiSceneScope presentation(m, hooks::Event::PartyMenu, 0);
    m.ax() = 0;
    m.ds() = 0;
    m.set_al(0);
    for (uint16_t off : {uint16_t(kTabActionIndex), uint16_t(kTabClassSelectIndex),
                         uint16_t(kTabGenderSelectIndex), uint16_t(kTabThirdSelection),
                         uint16_t(kTabListColumn), uint16_t(kTabSourceColumn), uint16_t(kTabExitRequested)})
        m.wr8(m.ds(), off, 0);
    m.ax() = 1;
    m.bx() = 0;
    m.cx() = 0x2020;
    m.dx() = 0x1FF;
    fill_screen_rect_core(m);
    bool full = true, redraw = true;
    for (;;) {
        if (full) {
            draw_equipment_action_cursor_core(m);
            draw_equipment_review_screen_core(m);
            dma_sprite_table_core(m);
            m.call_far(0x4000, 9);
            dma_palettes_core(m);
            m.ax() = 7;
            menu_out16(m, 0, m.ax());
            m.ax() = 8;
            menu_out16(m, 0x10, m.ax());
            m.ax() = 0xFC08;
            menu_out16(m, 0x12, m.ax());
            m.set_al(0x32);
            m.out8(7, m.al());
            full = false;
            redraw = true;
        }
        if (redraw) {
            draw_menu_selection_description_core(m);
            redraw = false;
        }
        party_menu(m, 3);
        item_menu_frame(m, 0x36E2, 0x1C);
        count_party_members_core(m);
        m.dx() = 0x0307;
        m.si() = m.rd8(m.ds(), kTabListColumn) ? kTabGenderSelectIndex : kTabClassSelectIndex;
        if (m.ax() & 0x200) {
            m.set_al(uint8_t(m.rd8(m.ds(), kTabEquipmentMember) + 1));
            find_last_party_member_core(m);
            if (m.al() > uint8_t(m.dx()))
                m.set_al(0);
            m.wr8(m.ds(), kTabEquipmentMember, m.al());
            m.si() = kTabEquipmentMember;
            select_next_equipment_member_core(m);
            menu_sound(m, 0x92);
            full = true;
            continue;
        }
        if (m.ax() & 0x800) {
            uint8_t selected_member = m.rd8(m.ds(), kTabEquipmentMember);
            m.set_al(selected_member ? uint8_t(selected_member - 1) : 3);
            m.wr8(m.ds(), kTabEquipmentMember, m.al());
            m.si() = kTabEquipmentMember;
            select_previous_equipment_member_core(m);
            menu_sound(m, 0x92);
            full = true;
            continue;
        }
        if (m.ax() & 8) {
            menu_sound(m, 0x94);
            break;
        }
        if (m.ax() & 4) {
            menu_sound(m, 0x93);
            party_menu(m, 0);
            select_equipment_item_action_core(m);
            if (m.rd8(m.ds(), kTabExitRequested))
                break;
            full = true;
            continue;
        }
        if (m.ax() & 0x50) {
            // L_342F72 tests down (0x40) before up (0x10): with both held, down wins
            menu_move(m, (m.ax() & 0x40) == 0, 7);
            menu_sound(m, 0x92);
            redraw = true;
        }
    }
    return;
}

// rom: 0x342E5B (near)
// Equipment item-use menu: select member/list/slot, dispatch its action, or leave on cancel/field transition.
void run_equipment_item_menu(Machine &m) {
    run_equipment_item_menu_core(m);
    m.ret();
}
namespace {
// Shared equipment-transfer panels. The outer panel selects a source, the inner panel selects its
// destination.
void equipment_transfer_panel(Machine &m, bool destination) {
    UiSceneScope presentation(m, hooks::Event::PartyMenu, 0);
    bool full = true, redraw = true;
    if (destination) {
        m.set_al(m.rd8(m.ss(), m.si()));
        m.wr8(m.ss(), kTabActionIndex, m.al());
        m.set_al(m.rd8(m.ss(), kTabListColumn));
        m.wr8(m.ss(), kTabSourceColumn, m.al());
        if (m.rd8(m.ss(), kTabListColumn) == 1)
            clear_equipment_inventory_arrow_core(m);
        else {
            clear_equipment_left_marker_core(m);
            clear_equipment_right_marker_core(m);
        }
        show_secondary_menu_cursor_core(m);
        if (m.al() == 8) {
            select_direction_destination_core(m);
            hide_secondary_menu_cursor_core(m);
            if (m.rd8(m.ss(), kTabListColumn))
                draw_equipment_inventory_arrow_core(m);
            return;
        }
        full = false;
    } else {
        m.ax() = 0;
        m.ds() = 0;
        m.set_al(0);
        for (uint16_t off : {uint16_t(kTabActionIndex), uint16_t(kTabClassSelectIndex),
                             uint16_t(kTabGenderSelectIndex), uint16_t(kTabListColumn),
                             uint16_t(kTabItemBuffer), uint16_t(kNameText), uint16_t(kTabExitRequested)})
            m.wr8(m.ds(), off, 0);
        m.wr8(m.ds(), kTabSourceColumn, 0xFF);
        m.ax() = 1;
        m.bx() = 0;
        m.cx() = 0x2020;
        m.dx() = 0x1FF;
        fill_screen_rect_core(m);
    }
    for (;;) {
        if (full) {
            equipment_slot_bounds_core(m);
            draw_equipment_selection_cursor_core(m);
            draw_equipment_edit_screen_core(m);
            dma_sprite_table_core(m);
            m.call_far(0x4000, 9);
            dma_palettes_core(m);
            m.ax() = 7;
            menu_out16(m, 0, m.ax());
            m.ax() = 8;
            menu_out16(m, 0x10, m.ax());
            m.ax() = 0xFC08;
            menu_out16(m, 0x12, m.ax());
            m.set_al(0x32);
            m.out8(7, m.al());
            full = false;
            redraw = true;
        }
        if (redraw) {
            draw_menu_selection_description_core(m);
            redraw = false;
        }
        m.set_al(0);
        m.out8(5, 0);
        m.set_al(0x1C);
        m.out8(6, m.al());
        equipment_slot_bounds_core(m);
        draw_equipment_selection_cursor_core(m);
        dma_sprite_table_core(m);
        m.call_far(0x4000, 9);
        dma_palettes_core(m);
        m.ax() = 7;
        menu_out16(m, 0, m.ax());
        m.ax() = 8;
        menu_out16(m, 0x10, m.ax());
        m.ax() = 0xFC08;
        menu_out16(m, 0x12, m.ax());
        m.set_al(0x32);
        m.out8(7, m.al());
        party_menu(m, destination ? 6 : 5);
        m.call_far(0x4000, 0x5c);
        m.ax() = m.rd16(m.ss(), kKeysRepeat);
        menu_push(m, m.ax());
        equipment_slot_bounds_core(m);
        m.si() = kTabClassSelectIndex;
        m.ax() = menu_pop(m);
        if (m.rd8(m.ss(), kTabListColumn)) {
            m.dx() = uint16_t((m.rd8(m.ss(), kTabItemBuffer) ? 8 : 7) << 8);
            m.si() = kTabGenderSelectIndex;
        }
        bool member_keys = !destination || m.rd8(m.ss(), kTabSourceColumn) != 0;
        if (member_keys && (m.ax() & 0xA00)) {
            bool next = (m.ax() & 0x200) != 0;
            uint8_t member = m.rd8(m.ss(), kTabEquipmentMember);
            m.set_al(next ? uint8_t(member + 1) : (member ? uint8_t(member - 1) : 3));
            if (next) {
                find_last_party_member_core(m);
                if (m.al() > uint8_t(m.dx()))
                    m.set_al(0);
            }
            m.wr8(m.ss(), kTabEquipmentMember, m.al());
            m.si() = kTabEquipmentMember;
            (next ? select_next_party_member_core : select_previous_party_member_core)(m);
            menu_sound(m, 0x92);
            if (destination)
                draw_equipment_edit_screen_core(m);
            else
                equipment_slot_bounds_core(m);
            full = !destination;
            redraw = true;
            continue;
        }
        if (m.ax() & 8) {
            if (destination)
                hide_secondary_menu_cursor_core(m);
            menu_sound(m, 0x94);
            break;
        }
        // L_343DE1 / L_343FC1: a monster's ability list (DL = 8 from equipment_slot_bounds) takes only the
        // member keys and B above; A and every direction go straight back to the key poll.
        if (uint8_t(m.dx()) == 8)
            continue;
        if (m.ax() & 4) {
            menu_sound(m, 0x93);
            party_menu(m, 0);
            (destination ? apply_equipment_transfer_choice_core
                         : select_equipment_transfer_destination_core)(m);
            if (destination) {
                if (m.flags() & emu::CF) {
                    redraw = true;
                    continue;
                }
                hide_secondary_menu_cursor_core(m);
                // Done: the original stays in the list the cursor ended in; the hook may take a transfer
                // from the gear list back to it (as the Left key does).
                if (m.rd8(m.ss(), kTabListColumn) && !m.rd8(m.ss(), kTabSourceColumn) &&
                    hooks::should(hooks::VB::EquipReturnToGear, m, false)) {
                    m.wr8(m.ss(), kTabListColumn, 0);
                    clear_equipment_inventory_arrow_core(m);
                }
                break;
            }
            m.wr8(m.ss(), kTabSourceColumn, 0xFF);
            if (m.rd8(m.ss(), kTabExitRequested))
                break;
            full = true;
            continue;
        }
        if (m.ax() & 0x50) {
            uint8_t selection = m.rd8(m.ss(), m.si()), minimum = uint8_t(m.dx()),
                    maximum = uint8_t(m.dx() >> 8);
            if (m.ax() & 0x40) {
                selection = uint8_t(selection + 1);
                if (selection > maximum)
                    selection = minimum;
            } else {
                uint8_t previous = selection;
                selection = uint8_t(selection - 1);
                if (!previous || selection < minimum)
                    selection = maximum;
            }
            m.set_al(selection);
            m.wr8(m.ss(), m.si(), selection);
            menu_sound(m, 0x92);
            redraw = true;
            continue;
        }
        if (m.ax() & 0x20) {
            if (m.rd8(m.ss(), kTabListColumn) == 1) {
                if (destination && (m.rd8(m.ss(), kTabSourceColumn) & 1))
                    continue;
                menu_sound(m, 0x92);
                uint8_t group = uint8_t(m.rd8(m.ss(), kTabItemBuffer) ^ 1);
                m.wr8(m.ss(), kTabItemBuffer, group);
                if (!group && m.rd8(m.ss(), m.si()) == 8)
                    m.wr8(m.ss(), m.si(), 7);
                if (destination)
                    draw_equipment_edit_screen_core(m);
                else
                    full = true;
                redraw = true;
            } else {
                menu_sound(m, 0x92);
                if (!m.rd8(m.ss(), kTabItemBuffer) && m.rd8(m.ss(), kTabGenderSelectIndex) == 8) {
                    m.si() = kTabGenderSelectIndex;
                    m.wr8(m.ss(), m.si(), 7);
                }
                m.wr8(m.ss(), kTabListColumn, 1);
                if (!destination || m.rd8(m.ss(), kTabSourceColumn) != 1)
                    draw_equipment_inventory_arrow_core(m);
                redraw = true;
            }
        } else if ((m.ax() & 0x80) && !m.rd8(m.ss(), kNameText) && m.rd8(m.ss(), kTabListColumn)) {
            m.wr8(m.ss(), kTabListColumn, 0);
            clear_equipment_inventory_arrow_core(m);
            menu_sound(m, 0x92);
            redraw = true;
        }
    }
    if (destination && m.rd8(m.ss(), kTabListColumn))
        draw_equipment_inventory_arrow_core(m);
}
} // namespace
// Shared body of ROM 0x343CBE
// Equipment transfer menu: choose actor, inventory group and source slot, then open the destination panel.
void run_equipment_transfer_menu_core(Machine &m) {
    equipment_transfer_panel(m, false);
    return;
}

// rom: 0x343CBE (near)
// Equipment transfer menu: choose actor, inventory group and source slot, then open the destination panel.
void run_equipment_transfer_menu(Machine &m) {
    run_equipment_transfer_menu_core(m);
    m.ret();
}
// Shared body of ROM 0x343EC3
// Select an equipment-transfer destination, retry invalid choices and restore the source list on exit.
void select_equipment_transfer_destination_core(Machine &m) {
    equipment_transfer_panel(m, true);
    return;
}

// rom: 0x343EC3 (near)
// Select an equipment-transfer destination, retry invalid choices and restore the source list on exit.
void select_equipment_transfer_destination(Machine &m) {
    select_equipment_transfer_destination_core(m);
    m.ret();
}
// Shared body of ROM 0x344BB9
// Select a second party member for the animated record swap; cancel restores the saved menu selection.
void select_party_swap_destination_core(Machine &m) {
    m.set_al(m.rd8(m.ds(), m.si()));
    m.wr8(m.ds(), kTabActionIndex, m.al());
    m.set_al(m.rd8(m.ds(), kTabListColumn));
    m.wr8(m.ds(), kTabSourceColumn, m.al());
    show_secondary_menu_cursor_core(m);
    if (m.al() == 8) {
        select_direction_destination_core(m);
        hide_secondary_menu_cursor_core(m);
        return;
    }
    bool redraw = true;
    for (;;) {
        if (redraw) {
            draw_menu_selection_description_core(m);
            redraw = false;
        }
        draw_play_time_core(m);
        // The redesigned order screen stays up through the native swap animation; the records swap after it.
        party_menu(m, 8);
        item_menu_frame(m, 0x4F15, 0x1C);
        m.dx() = uint16_t((m.dx() & 0xFF00) | 3);
        m.si() = kTabClassSelectIndex;
        if (m.ax() & 8) {
            hide_secondary_menu_cursor_core(m);
            menu_sound(m, 0x94);
            break;
        }
        if (m.ax() & 4) {
            menu_sound(m, 0x93);
            swap_selected_party_members_core(m);
            if (m.flags() & emu::CF) {
                redraw = true;
                continue;
            }
            hide_secondary_menu_cursor_core(m);
            break;
        }
        if (m.ax() & 0x50) {
            // L_344C3B tests down (0x40) before up (0x10): with both held, down wins
            bool up = (m.ax() & 0x40) == 0;
            menu_move(m, up, 3);
            (up ? select_previous_equipment_member_core : select_next_equipment_member_core)(m);
            menu_sound(m, 0x92);
            redraw = true;
        }
    }
    return;
}

// rom: 0x344BB9 (near)
// Select a second party member for the animated record swap; cancel restores the saved menu selection.
void select_party_swap_destination(Machine &m) {
    select_party_swap_destination_core(m);
    m.ret();
}
// Shared body of ROM 0x344E09
// Update/draw the party-swap animation and wait a frame, returning newly pressed keys and ZF.
void update_party_swap_frame_core(Machine &m) {
    menu_save(m);
    update_party_swap_tiles_core(m);
    m.ax() = m.rd16(m.ss(), kTabItemBuffer);
    m.dx() = m.rd16(m.ss(), kTabSecondItemBuffer);
    m.bx() = m.rd16(m.ss(), kTabTransferState);
    set_sprite_16x16_core(m);
    dma_sprite_table_core(m);
    m.call_far(0x4000, 9);
    menu_restore(m);
    m.call_far(0x4000, 0x5c);
    m.ax() = m.rd16(m.ss(), kKeysPressed);
    menu_test(m, m.ax());
    return;
}

// rom: 0x344E09 (near)
// Update/draw the party-swap animation and wait a frame, returning newly pressed keys and ZF.
void update_party_swap_frame(Machine &m) {
    update_party_swap_frame_core(m);
    m.ret();
}
// Shared body of ROM 0x344DF5
// Advance CX swap-animation frames, stopping on input.
void wait_party_swap_frames_core(Machine &m) {
    do {
        update_party_swap_frame_core(m);
        if (!(m.flags() & emu::ZF)) {
            menu_sound(m, 0x93);
            m.ax() = 1;
            menu_test(m, 1);
            return;
        }
    } while (--m.cx());
    return;
}

// rom: 0x344DF5 (near)
// Advance CX swap-animation frames, stopping on input.
void wait_party_swap_frames(Machine &m) {
    wait_party_swap_frames_core(m);
    m.ret();
}
// Shared body of ROM 0x344D16
// Animate a party-member sprite between its old and new rows; stop at any newly pressed key.
void animate_party_member_swap_core(Machine &m) {
    for (uint16_t sprite : {uint16_t(0), uint16_t(4)}) {
        m.ax() = sprite;
        m.bx() = 0xE0;
        m.dx() = 0x3FFB;
        set_sprite_16x16_core(m);
    }
    m.ax() = uint16_t(m.rd8(m.ss(), kTabActionIndex) * 8);
    m.si() = uint16_t(0x4EF5 + m.ax());
    m.ax() = m.rd16(m.cs(), m.si());
    m.wr16(m.ss(), kTabItemBuffer, m.ax());
    m.ax() = m.rd16(m.cs(), uint16_t(m.si() + 4));
    m.wr16(m.ss(), kTabSecondItemBuffer, m.ax());
    m.ax() = m.rd16(m.cs(), uint16_t(m.si() + 2));
    m.wr16(m.ss(), kTabTransferState, m.ax());
    m.wr8(m.ss(), kTabTransferPage, 4);
    m.wr8(m.ss(), kTabTransferResult, 0xFF);
    m.cx() = 20;
    wait_party_swap_frames_core(m);
    if (!(m.flags() & emu::ZF)) {
        return;
    }
    m.cx() = 16;
    do {
        m.wr8(m.ss(), kTabTransferState, uint8_t(m.rd8(m.ss(), kTabTransferState) - 1));
        update_party_swap_frame_core(m);
        if (!(m.flags() & emu::ZF)) {
            return;
        }
    } while (--m.cx());
    m.cx() = 16;
    wait_party_swap_frames_core(m);
    if (!(m.flags() & emu::ZF)) {
        return;
    }
    m.cx() &= 0xFF;
    m.set_al(m.rd8(m.ss(), kTabClassSelectIndex));
    m.dx() = uint16_t((m.dx() & 0xFF00) | 20);
    m.ax() = uint16_t(m.al() * 20);
    m.set_al(uint8_t(m.al() + 16));
    m.bx() = uint16_t((m.bx() & 0xFF00) | m.rd8(m.ss(), kTabTransferCount));
    if (m.al() > uint8_t(m.bx())) {
        m.wr8(m.ss(), kTabTransferPage, 0);
        m.wr8(m.ss(), kTabTransferResult, 0xFF);
        m.set_al(uint8_t(m.al() - uint8_t(m.bx())));
        m.cx() = m.al();
        m.dx() = uint16_t((m.dx() & 0xFF00) | 1);
    } else {
        m.wr8(m.ss(), kTabTransferPage, 2);
        m.wr8(m.ss(), kTabTransferResult, 0xFF);
        m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(uint8_t(m.bx()) - m.al()));
        m.cx() = uint8_t(m.bx());
        m.dx() = uint16_t((m.dx() & 0xFF00) | 0xFF);
    }
    do {
        m.set_al(uint8_t(m.rd8(m.ss(), kTabTransferCount) + uint8_t(m.dx())));
        m.wr8(m.ss(), kTabTransferCount, m.al());
        update_party_swap_frame_core(m);
        if (!(m.flags() & emu::ZF)) {
            return;
        }
    } while (--m.cx());
    m.wr8(m.ss(), kTabTransferPage, 6);
    m.wr8(m.ss(), kTabTransferResult, 0xFF);
    m.cx() = 12;
    wait_party_swap_frames_core(m);
    if (!(m.flags() & emu::ZF)) {
        return;
    }
    draw_party_swap_label_core(m);
    m.cx() = 60;
    wait_party_swap_frames_core(m);
    m.set_al(uint8_t(m.in8(6) - 6));
    m.out8(6, m.al());
    return;
}

// rom: 0x344D16 (near)
// Animate a party-member sprite between its old and new rows; stop at any newly pressed key.
void animate_party_member_swap(Machine &m) {
    animate_party_member_swap_core(m);
    m.ret();
}
namespace {
void draw_list_cursor(Machine &m, uint16_t selection, uint16_t table, uint16_t sprite) {
    m.bx() = uint16_t(m.rd8(m.ss(), selection) * 2);
    m.ax() = m.rd16(m.cs(), uint16_t(m.bx() + table));
    m.bx() = m.ax();
    m.wr16(m.ss(), kMenuCursorPosition, m.bx());
    m.ax() = sprite;
    m.dx() = 0x3FFB;
    set_sprite_16x16_core(m);
}
} // namespace
// Shared body of ROM 0x345546
// Place the creation equipment cursor, also writing its packed position at SS:014A (original quirk).
void draw_growth_review_cursor_core(Machine &m) {
    draw_list_cursor(m, kTabFourthSelection, 0x5567, 4);
    return;
}

// rom: 0x345546 (near)
// Place the creation equipment cursor, also writing its packed position at SS:014A (original quirk).
void draw_growth_review_cursor(Machine &m) {
    draw_growth_review_cursor_core(m);
    m.ret();
}
// Shared body of ROM 0x34895D
// Draw history-member cursor from D846, writing packed position to SS:014A.
void draw_bestiary_category_cursor_core(Machine &m) {
    draw_list_cursor(m, kTabFifthSelection, 0x897E, 0);
    return;
}

// rom: 0x34895D (near)
// Draw history-member cursor from D846, writing packed position to SS:014A.
void draw_bestiary_category_cursor(Machine &m) {
    draw_bestiary_category_cursor_core(m);
    m.ret();
}
// Shared body of ROM 0x348986
// Draw history-row cursor from D847, writing packed position to SS:014A.
void draw_bestiary_list_cursor_core(Machine &m) {
    draw_list_cursor(m, kTabSixthSelection, 0x89A7, 4);
    return;
}

// rom: 0x348986 (near)
// Draw history-row cursor from D847, writing packed position to SS:014A.
void draw_bestiary_list_cursor(Machine &m) {
    draw_bestiary_list_cursor_core(m);
    m.ret();
}
// Shared body of ROM 0x3485B8
// Look up and dispatch an item's field effect for the actor in DI/BP; unsupported items display an error.
void apply_selected_item_effect_core(Machine &m) {
    menu_save(m);
    m.ax() = 0x9000;
    m.es() = m.ax();
    m.bx() = uint16_t(m.rd8(m.ss(), m.si()) * 2 + 0x41E0);
    m.bx() = m.rd16(m.es(), m.bx());
    uint8_t usable = table_at(m, Table::item_records, m.es(), m.bx()) & 2;
    menu_test(m, usable);
    if (usable) {
        m.set_al(m.rd8(m.ss(), m.si()));
        if (m.al() >= 0x7A) {
            m.set_al(uint8_t(m.al() - 0x66));
            if (m.al() >= 0x1A)
                m.set_al(uint8_t(m.al() - 2));
        }
        menu_push(m, m.bx());
        m.set_ah(0);
        m.ax() = uint16_t(m.ax() * 2);
        m.bx() = uint16_t(0x8604 + m.ax());
        // ADD BX,AX is live through effect handlers that leave arithmetic flags untouched.
        uint16_t sum = m.bx(), rhs = m.ax();
        m.flags() =
            uint16_t((m.flags() & ~(emu::CF | emu::PF | emu::AF | emu::ZF | emu::SF | emu::OF)) |
                     (sum < 0x8604 ? emu::CF : 0) | (sum == 0 ? emu::ZF : 0) | (sum & 0x8000 ? emu::SF : 0) |
                     ((std::popcount(uint8_t(sum)) & 1) == 0 ? emu::PF : 0) |
                     ((0x8604 ^ rhs ^ sum) & 0x10 ? emu::AF : 0) |
                     ((~(0x8604 ^ rhs) & (0x8604 ^ sum) & 0x8000) ? emu::OF : 0));
        m.ax() = m.rd16(m.cs(), m.bx());
        m.bx() = menu_pop(m);
        dispatch_computed_handler_core(m, m.ax());
    } else {
        m.ax() = 0;
        {
            const uint16_t native_caller_cs = m.cs();
            m.cs() = 0x4000;
            wait_unusable_item_message_core(m);
            m.cs() = native_caller_cs;
        }
        m.flags() |= emu::CF;
    }
    menu_restore(m);
    return;
}

// rom: 0x3485B8 (near)
// Look up and dispatch an item's field effect for the actor in DI/BP; unsupported items display an error.
void apply_selected_item_effect(Machine &m) {
    apply_selected_item_effect_core(m);
    m.ret();
}
// Shared body of ROM 0x349545
// Display a selected menu error, wait for any new key, then erase its row; preserve DS.
void wait_menu_error_message_core(Machine &m) {
    hooks::EventScope service_scope(
        m, hooks::Event::ServiceMenu,
        {13, 1, int32_t(TextKey::menu_error_0) + std::min<uint8_t>(uint8_t(m.ax()), 9)}, {0, -1});
    uint16_t ds = m.ds();
    m.ds() = m.cs();
    menu_push(m, m.ax());
    clear_inn_message_core(m);
    m.ax() = menu_pop(m);
    {
        const uint8_t index = uint8_t(m.ax()); // D_349586[index]: the 10 error lines (keys menu_error_N)
        const TextKey key = TextKey(uint16_t(TextKey::menu_error_0) + std::min<uint8_t>(index, 9));
        const TextPtr t =
            index < 10 ? text(m, key) : TextPtr{m.cs(), m.rd16(m.cs(), uint16_t(0x9586 + index * 2))};
        m.ds() = t.segment;
        m.si() = t.offset;
        m.ax() = 1;
        m.bx() = 0x0E05;
        draw_text_core(m);
        text_done(m, key);
    }
    wait_menu_key(m);
    {
        const TextPtr t = text(m, TextKey::wait_menu_error_message_1);
        m.ds() = t.segment;
        m.ax() = 1;
        m.bx() = 0x0E05;
        m.si() = t.offset;
        draw_text_core(m);
        text_done(m, TextKey::wait_menu_error_message_1);
    }
    m.ds() = ds;
    return;
}

// rom: 0x349545 (near)
// Display a selected menu error, wait for any new key, then erase its row; preserve DS.
void wait_menu_error_message(Machine &m) {
    wait_menu_error_message_core(m);
    m.ret();
}
// Shared body of ROM 0x347079
// Select one of nine field-menu assignments for the option indexed by D843.
void select_field_menu_assignment_core(Machine &m) {
    m.bx() = uint16_t(kPartyOrder + m.rd8(m.ss(), kTabGenderSelectIndex));
    m.set_al(uint8_t(m.rd8(m.ss(), m.bx()) - 1));
    m.wr8(m.ss(), kTabThirdSelection, m.al());
    m.di() = 0x7126;
    draw_options_secondary_cursor_core(m);
    draw_field_menu_label_core(m);
    draw_dialogue_member_list_core(m);
    dma_sprite_table_core(m);
    m.call_far(0x4000, 9);
    dma_palettes_core(m);
    for (;;) {
        m.call_far(0x4000, 0x5c);
        m.ax() = m.rd16(m.ds(), kKeysRepeat);
        m.dx() = uint16_t((m.dx() & 0xFF00) | 8);
        m.si() = kTabThirdSelection;
        if (m.ax() & 8) {
            menu_sound(m, 0x94);
            break;
        }
        if (m.ax() & 4) {
            menu_sound(m, 0x93);
            m.bx() = uint16_t(kPartyOrder + m.rd8(m.ss(), kTabGenderSelectIndex));
            m.set_al(uint8_t(m.rd8(m.ss(), kTabThirdSelection) + 1));
            m.wr8(m.ss(), m.bx(), m.al());
            break;
        }
        if (m.ax() & 0x50) {
            // rom: L_347104/L_34710F use `ss:[si]`, not DS.
            menu_move(m, (m.ax() & 0x10) != 0, 8, m.ss());
            menu_sound(m, 0x92);
        }
        m.di() = 0x7126;
        draw_options_secondary_cursor_core(m);
        menu_present(m);
    }
    return;
}

// rom: 0x347079 (near)
// Select one of nine field-menu assignments for the option indexed by D843.
void select_field_menu_assignment(Machine &m) {
    select_field_menu_assignment_core(m);
    m.ret();
}
// Shared body of ROM 0x3453D3
// Browse growth/class review pages and rows, skipping locked pages through the original page helpers.
void run_growth_review_menu_core(Machine &m) {
    UiSceneScope bestiary_scope(m, hooks::Event::Bestiary, 6); // presentation only
    m.ax() = 0;
    m.ds() = 0;
    m.set_al(0);
    bool full = true;
    for (;;) {
        if (full) {
            draw_growth_review_cursor_core(m);
            draw_creation_type_detail_screen_core(m);
            draw_creation_item_description_core(m);
            dma_sprite_table_core(m);
            m.call_far(0x4000, 9);
            dma_palettes_core(m);
            full = false;
        }
        m.call_far(0x4000, 0x5c);
        m.ax() = m.rd16(m.ss(), kKeysRepeat);
        m.dx() = uint16_t((m.dx() & 0xFF00) | 7);
        m.si() = kTabFourthSelection;
        if (m.ax() & 8) {
            menu_sound(m, 0x94);
            break;
        }
        if (m.ax() & 4) {
            full = true;
            continue;
        }
        if (m.ax() & 0x50) {
            // rom: L_34543C/L_345447 use `ss:[si]`, not DS.
            menu_move(m, (m.ax() & 0x10) != 0, 7, m.ss());
            draw_creation_item_description_core(m);
            menu_sound(m, 0x92);
        } else if (m.ax() & 0xA00) {
            bool prev = (m.ax() & 0x800) != 0;
            m.dx() = uint16_t((m.dx() & 0xFF00) | 5);
            m.si() = kTabGenderSelectIndex;
            // rom: L_345462/L_34547F use `ss:[si]`, not DS.
            menu_move(m, prev, 5, m.ss());
            if (prev)
                select_previous_creation_type_core(m);
            else
                select_next_creation_type_core(m);
            menu_sound(m, 0x92);
            full = true;
            continue;
        }
        draw_growth_review_cursor_core(m);
        menu_present(m);
    }
    draw_class_review_cursor_core(m);
    return;
}

// rom: 0x3453D3 (near)
// Browse growth/class review pages and rows, skipping locked pages through the original page helpers.
void run_growth_review_menu(Machine &m) {
    run_growth_review_menu_core(m);
    m.ret();
}
// Shared body of ROM 0x34889B
// Browse the bestiary list with a fourteen-row viewport and scroll offset; cancel leaves the panel.
void browse_bestiary_list_core(Machine &m) {
    m.ax() = 0;
    m.ds() = 0;
    m.wr8(m.ss(), kTabSixthSelection, 0);
    m.wr16(m.ss(), kTabEquipmentMember, 0);
    uint16_t ds = m.ds();
    {
        const TextPtr t = text(m, TextKey::browse_bestiary_list_1);
        m.ds() = t.segment;
        m.ax() = 1;
        m.bx() = 0x0B03;
        m.si() = t.offset;
        draw_text_core(m);
        text_done(m, TextKey::browse_bestiary_list_1);
    }
    m.ds() = ds;
    hooks::emit(hooks::Event::Bestiary, m, {2}); // back to 1 when run_monster_history_menu redraws
    draw_bestiary_list_cursor_core(m);
    dma_sprite_table_core(m);
    m.call_far(0x4000, 9);
    dma_palettes_core(m);
    for (;;) {
        m.call_far(0x4000, 0x5c);
        m.ax() = uint16_t(hooks::filter(hooks::Value::BestiaryKeys, m, m.rd16(m.ss(), kKeysRepeat)));
        m.dx() = uint16_t((m.dx() & 0xFF00) | 13);
        m.si() = kTabSixthSelection;
        if (m.ax() & 8) {
            menu_sound(m, 0x94);
            break;
        }
        if (m.ax() & 0x50) {
            bool previous = (m.ax() & 0x10) != 0;
            m.set_al(m.rd8(m.ss(), m.si()));
            if (previous) {
                if (m.al())
                    m.set_al(uint8_t(m.al() - 1));
                else if (m.rd16(m.ss(), kTabEquipmentMember))
                    m.wr16(m.ss(), kTabEquipmentMember, uint16_t(m.rd16(m.ss(), kTabEquipmentMember) - 1));
            } else if (m.al() != 13)
                m.set_al(uint8_t(m.al() + 1));
            else {
                m.bx() = uint16_t(m.rd16(m.ss(), kTabEquipmentMember) + 14);
                if (m.bx() < m.rd16(m.ss(), kTabItemBuffer))
                    m.wr16(m.ss(), kTabEquipmentMember, uint16_t(m.rd16(m.ss(), kTabEquipmentMember) + 1));
            }
            m.wr8(m.ss(), m.si(), m.al());
            draw_monster_history_page_core(m);
            menu_sound(m, 0x92);
        }
        draw_bestiary_list_cursor_core(m);
        menu_present(m);
    }
    return;
}

// rom: 0x34889B (near)
// Browse the bestiary list with a fourteen-row viewport and scroll offset; cancel leaves the panel.
void browse_bestiary_list(Machine &m) {
    browse_bestiary_list_core(m);
    m.ret();
}
// Shared body of ROM 0x347B09
// Validate the selected save slot, displaying its load prompt or an error that waits for input.
void draw_load_slot_prompt_core(Machine &m) {
    menu_save(m);
    m.ax() = 0;
    m.ds() = 0;
    m.ax() = 1;
    m.bx() = 1;
    m.cx() = 0x021C;
    m.dx() = 0x1FF;
    fill_screen_rect_core(m);
    m.ax() = save_slot_for_row(m, m.rd8(m.ss(), kTabClassSelectIndex));
    m.di() = kSaveScratch;
    load_save_slot_core(m);
    bool invalid = (m.flags() & emu::CF) != 0;
    if (invalid) {
        hooks::emit(hooks::Event::SaveMenu, m, {6});
        m.ax() = 0x1C;
        m.bx() = 0xE0;
        m.dx() = 0x3FFB;
        set_sprite_16x16_core(m);
        m.wr8(m.ss(), kTabGenderSelectIndex, 0);
        m.ax() = 0;
        m.bx() = 8;
        m.cx() = 0x0315;
        draw_window_core(m);
        static constexpr TextKey kInvalidKeys[2] = {TextKey::draw_load_slot_prompt_1,
                                                    TextKey::draw_load_slot_prompt_2};
        for (TextKey key : kInvalidKeys) {
            const TextPtr t = text(m, key);
            m.ds() = t.segment;
            m.ax() = 1;
            m.bx() = 9;
            m.si() = t.offset;
            draw_text_core(m);
            text_done(m, key);
        }
        menu_sound(m, 0x2A);
        wait_menu_key(m);
        m.flags() |= emu::CF;
    } else {
        m.si() = kSaveScratch;
        bool creation = m.rd8(m.ss(), uint16_t(m.si() + 0xE4)) != 0;
        m.wr8(m.ss(), kTabGenderSelectIndex, 0);
        m.ax() = 0;
        m.bx() = 1;
        m.cx() = 0x031C;
        draw_window_core(m);
        static constexpr TextKey kCreationFalseKeys[2] = {TextKey::draw_load_slot_prompt_3,
                                                          TextKey::draw_load_slot_prompt_4};
        static constexpr TextKey kCreationTrueKeys[2] = {TextKey::draw_load_slot_prompt_6,
                                                         TextKey::draw_load_slot_prompt_7};
        for (int i = 0; i < 2; ++i) {
            const TextKey key = creation ? kCreationTrueKeys[i] : kCreationFalseKeys[i];
            const TextPtr t = text(m, key);
            m.ds() = t.segment;
            m.ax() = 1;
            m.bx() = i ? 0x117 : 0x113;
            m.si() = t.offset;
            draw_text_core(m);
            text_done(m, key);
        }
        {
            const TextKey key =
                creation ? TextKey::draw_load_slot_prompt_8 : TextKey::draw_load_slot_prompt_5;
            const TextPtr t = text(m, key);
            m.ds() = t.segment;
            m.ax() = 1;
            m.bx() = 2;
            m.si() = t.offset;
            draw_text_core(m);
            text_done(m, key);
        }
        if (creation) {
            const TextPtr t = text(m, TextKey::draw_load_slot_prompt_9);
            m.ds() = t.segment;
            m.ax() = 1;
            m.bx() = 0x102;
            m.si() = t.offset;
            draw_text_core(m);
            text_done(m, TextKey::draw_load_slot_prompt_9);
        }
        m.flags() &= ~emu::CF;
    }
    menu_restore(m);
    return;
}

// rom: 0x347B09 (near)
// Validate the selected save slot, displaying its load prompt or an error that waits for input.
void draw_load_slot_prompt(Machine &m) {
    draw_load_slot_prompt_core(m);
    m.ret();
}
// Shared body of ROM 0x347854
// Confirm loading a validated save.
void confirm_load_slot_core(Machine &m) {
    UiSceneScope ui_scope(m, hooks::Event::SaveMenu, 3);
    m.ax() = 0;
    m.ds() = 0;
    m.set_al(1);
    m.wr8(m.ds(), kTabGenderSelectIndex, 1);
    m.di() = 0x79B5;
    draw_gender_select_cursor_core(m);
    draw_load_slot_prompt_core(m);
    auto cancel = [&](uint16_t sound) {
        m.ax() = sound;
        trigger_sound_effect_core(m);
        m.flags() &= ~emu::CF;
        return;
    };
    if (m.flags() & emu::CF) {
        cancel(0x94);
        return;
    }
    dma_sprite_table_core(m);
    m.call_far(0x4000, 9);
    dma_palettes_core(m);
    for (;;) {
        m.call_far(0x4000, 0x5c);
        m.ax() = m.rd16(m.ds(), kKeysRepeat);
        m.dx() = uint16_t((m.dx() & 0xFF00) | 1);
        m.si() = kTabGenderSelectIndex;
        if ((m.ax() & 8) || ((m.ax() & 4) && m.rd8(m.ss(), kTabGenderSelectIndex))) {
            cancel(0x94);
            return;
        }
        if (m.ax() & 4) {
            m.ax() = save_slot_for_row(m, m.rd8(m.ss(), kTabClassSelectIndex));
            m.di() = kSaveScratch;
            load_save_slot_core(m);
            if (m.flags() & emu::CF) {
                cancel(0x2A);
                return;
            }
            menu_sound(m, 0x93);
            write_last_save_slot_core(m);
            m.si() = kSaveScratch;
            m.di() = kGameState;
            m.cx() = 0x439;
            m.ds() = m.ss();
            m.es() = m.ss();
            m.flags() &= ~emu::DF;
            do {
                m.wr8(m.es(), m.di(), m.rd8(m.ds(), m.si()));
                ++m.si();
                ++m.di();
            } while (--m.cx());
            m.di() = kGameState;
            if (auto *s = Session::find(m.wsc()); s && s->player_names().enabled) {
                const int slot = save_slot_for_row(m, m.rd8(m.ss(), kTabClassSelectIndex));
                s->player_names().values = s->save_slots().holds(slot) && s->save_slots().has(slot)
                                               ? s->save_slots().names(slot)
                                               : PartyNameList{};
            }
            if (!m.rd8(m.ss(), uint16_t(m.di() + 0xE4))) {
                menu_compare(m, 0, 0, true);
                m.flags() |= emu::CF;
                return;
            }
            clear_game_state_core(m);
            m.si() = kSaveScratch;
            m.di() = kGameState;
            m.set_al(m.rd8(m.ss(), uint16_t(m.si() + 0xE5)));
            m.wr8(m.ss(), uint16_t(m.di() + 0xE5), m.al());
            m.ax() = 0;
            m.ds() = 0;
            m.es() = 0;
            m.si() = (kSaveScratch + 0xE6);
            m.di() = kDialogueSpeed;
            m.cx() = 31;
            do {
                m.wr8(m.es(), m.di(), m.rd8(m.ds(), m.si()));
                ++m.si();
                ++m.di();
            } while (--m.cx());
            m.set_al(0);
            m.bx() = uint16_t(m.bx() & 0xFF00);
            hooks::emit(hooks::Event::SaveMenu, m, {0}); // creation owns the picture, including its fade-in
            new_game_setup_core(m);
            uint8_t actor = m.rd8(m.ss(), kGameState);
            menu_compare(m, actor, 0xFF, true);
            if (actor != 0xFF) {
                m.flags() |= emu::CF;
                return;
            }
            prepare_party_menu_entry_core(m);
            m.ax() = 0;
            m.ds() = 0;
            m.set_al(0);
            for (uint16_t off : {uint16_t(kTabClassSelectIndex), uint16_t(kTabGenderSelectIndex),
                                 uint16_t(kTabExitRequested), uint16_t(kTabActionResult)})
                m.wr8(m.ds(), off, 0);
            read_last_save_slot_core(m);
            m.wr8(m.ds(), kTabClassSelectIndex, m.al());
            m.ax() = 1;
            m.bx() = 0;
            m.cx() = 0x2020;
            m.dx() = 0x1FF;
            fill_screen_rect_core(m);
            m.ax() = 7;
            menu_out16(m, 0, m.ax());
            m.ax() = 8;
            menu_out16(m, 0x10, m.ax());
            m.ax() = 0xFC08;
            menu_out16(m, 0x12, m.ax());
            m.set_al(0x32);
            m.out8(7, m.al());
            m.set_al(0);
            m.out8(5, 0);
            m.set_al(0x20);
            m.out8(6, m.al());
            m.flags() &= ~emu::CF;
            return;
        }
        if (m.ax() & 0xA0) {
            // rom: L_347992/L_34799D use `ss:[si]`, not DS.
            menu_move(m, (m.ax() & 0x80) != 0, 1, m.ss());
            menu_sound(m, 0x92);
        }
        m.di() = 0x79B5;
        draw_gender_select_cursor_core(m);
        menu_present(m);
    }
}

// rom: 0x347854 (near)
// Confirm loading a validated save.
void confirm_load_slot(Machine &m) {
    confirm_load_slot_core(m);
    m.ret();
}

namespace {
// Indirect menu tabs call these same helpers; keep the near-call logic shared with their entries.
void tab_call(Machine &m, uint16_t target) {
    switch (target) {
    case 0x1FB7:
        run_inventory_tab_core(m);
        break;
    case 0x2D43:
        run_equipment_tab_core(m);
        break;
    case 0x3BA7:
        run_transfer_tab_core(m);
        break;
    case 0x4AB1:
        run_formation_tab_core(m);
        break;
    case 0x5139:
        run_progress_tab_core(m);
        break;
    case 0x5FFD:
        run_save_tab_core(m);
        break;
    case 0x6B29:
        run_options_tab_core(m);
        break;
    case 0x28E4:
        draw_class_or_gender_cursor_core(m);
        break;
    case 0x2B08:
        draw_inventory_screen_core(m);
        break;
    case 0x8262:
        draw_menu_selection_description_core(m);
        break;
    case 0x20E7:
        select_class_action_core(m);
        break;
    case 0x1A9F:
        draw_party_overview_core(m);
        break;
    case 0x453B:
        draw_creation_step_cursor_core(m);
        break;
    case 0x1F5A:
        draw_play_time_core(m);
        break;
    case 0x2E5B:
        run_equipment_item_menu_core(m);
        break;
    case 0x14C1:
        prepare_party_menu_entry_core(m);
        break;
    case 0x3B45:
        select_next_equipment_member_core(m);
        break;
    case 0x3B76:
        select_previous_equipment_member_core(m);
        break;
    case 0x3CBE:
        run_equipment_transfer_menu_core(m);
        break;
    case 0x4706:
        select_next_party_member_core(m);
        break;
    case 0x472F:
        select_previous_party_member_core(m);
        break;
    case 0x4F15:
        draw_class_menu_cursor_core(m);
        break;
    case 0x4F46:
        draw_review_screen_core(m);
        break;
    case 0x4BB9:
        select_party_swap_destination_core(m);
        break;
    case 0x54F0:
        draw_class_review_cursor_core(m);
        break;
    case 0x5577:
        draw_race_selection_screen_core(m);
        break;
    case 0x5CA5:
        draw_creation_races_entry_core(m);
        break;
    case 0x6B04:
        read_last_save_slot_core(m);
        break;
    case 0x61C1:
        draw_class_select_cursor_core(m);
        break;
    case 0x61FF:
        draw_save_menu_core(m);
        break;
    case 0x653B:
        draw_selected_save_party_core(m);
        break;
    case 0x6102:
        confirm_save_slot_core(m);
        break;
    case 0x7204:
        draw_field_menu_cursor_core(m);
        break;
    case 0x7261:
        draw_field_menu_label_core(m);
        break;
    case 0x728D:
        draw_options_screen_core(m);
        break;
    default:
        dispatch_computed_handler_core(m, target);
        break;
    }
}
void tab_display(Machine &m) {
    m.ax() = 7;
    menu_out16(m, 0, m.ax());
    m.ax() = 8;
    menu_out16(m, 0x10, m.ax());
    m.ax() = 0xFC08;
    menu_out16(m, 0x12, m.ax());
    m.set_al(0x32);
    m.out8(7, m.al());
}
void tab_clear(Machine &m) {
    m.ax() = 1;
    m.bx() = 0;
    m.cx() = 0x2020;
    m.dx() = 0x1FF;
    fill_screen_rect_core(m);
}
void tab_upload(Machine &m) {
    dma_sprite_table_core(m);
    m.call_far(0x4000, 9);
    dma_palettes_core(m);
}
// Selected byte wraps over 0..DL; SI and all incidental AL effects match the original.
void tab_move(Machine &m, bool forward, bool stack_segment = false) {
    const uint16_t seg = stack_segment ? m.ss() : m.ds();
    const uint8_t previous_selection = m.rd8(seg, m.si());
    m.set_al(forward ? uint8_t(previous_selection + 1) : uint8_t(previous_selection - 1));
    if (forward ? m.al() > uint8_t(m.dx()) : previous_selection == 0)
        m.set_al(forward ? 0 : uint8_t(m.dx()));
    m.wr8(seg, m.si(), m.al());
}
void tab_poll(Machine &m, uint8_t last, uint16_t selected, bool stack_segment = false) {
    m.call_far(0x4000, 0x5C);
    m.ax() = m.rd16(stack_segment ? m.ss() : m.ds(), kKeysRepeat);
    m.dx() = uint16_t((m.dx() & 0xFF00) | (last));
    m.si() = selected;
}
// Equipment, transfer and formation tabs share setup/upload/key handling, with distinct action/cursors.
void run_member_tab_core(Machine &m, unsigned tab) {
    UiSceneScope presentation(m, hooks::Event::PartyMenu, 0);
    m.ax() = 0;
    m.ds() = 0;
    for (uint16_t off :
         {uint16_t(kTabActionIndex), uint16_t(kTabClassSelectIndex), uint16_t(kTabGenderSelectIndex),
          uint16_t(kTabThirdSelection), uint16_t(kTabListColumn), uint16_t(kTabEquipmentMember),
          uint16_t(kTabSourceColumn), uint16_t(kTabTransferState), uint16_t(kTabTransferCount),
          uint16_t(kTabTransferPage), uint16_t(kTabTransferResult), uint16_t(kTabExitRequested)})
        m.wr8(m.ds(), off, 0);
    m.wr16(m.ds(), kTabItemBuffer, 0);
    m.wr16(m.ds(), kTabSecondItemBuffer, 0);
    tab_call(m, 0x1A9F);
    m.ax() = 1;
    m.bx() = 0;
    m.cx() = 0x2020;
    m.dx() = 0x1FF;
    bool redraw = true;
    for (;;) {
        if (redraw) {
            tab_call(m, tab == 2 ? 0x4F15 : 0x453B);
            if (tab == 2)
                tab_call(m, 0x4F46);
            else {
                m.ax() = 0x18;
                m.bx() = tab ? 0x2892 : 0x2092;
                m.dx() = 0x3FFB;
                set_sprite_16x16_core(m);
            }
            tab_upload(m);
            tab_display(m);
        }
        tab_call(m, 0x1F5A);
        m.set_al(0);
        m.out8(5, m.al());
        m.set_al(0x1C);
        m.out8(6, m.al());
        tab_call(m, tab == 2 ? 0x4F15 : 0x453B);
        tab_upload(m);
        tab_display(m);
        party_menu(m, tab == 2 ? 7 : tab ? 4 : 2);
        tab_poll(m, 3, tab == 2 ? kTabClassSelectIndex : kTabEquipmentMember);
        const uint16_t keys = m.ax();
        redraw = false;
        if (keys & 8) {
            menu_sound(m, 0x94);
            return;
        }
        if (keys & 4) {
            menu_sound(m, 0x93);
            if (tab != 2) // order's destination choice keeps the redesigned screen
                party_menu(m, 0);
            tab_call(m, tab == 0 ? 0x2E5B : tab == 1 ? 0x3CBE : 0x4BB9);
            if (tab == 1)
                tab_call(m, 0x1A9F);
            if (m.rd8(m.ds(), kTabExitRequested))
                return;
            if (tab == 0) {
                tab_call(m, 0x14C1);
                tab_call(m, 0x1A9F);
            }
            redraw = true;
        } else if (keys & (0x40 | 0x10)) {
            const bool forward = (keys & 0x40) != 0;
            tab_move(m, forward);
            tab_call(m, tab == 1 ? (forward ? 0x4706 : 0x472F) : (forward ? 0x3B45 : 0x3B76));
            menu_sound(m, 0x92);
        }
    }
}
} // namespace
// rom: 0x342D43 (near)
// Equipment tab: select a party member and open that member's item-action menu.
void run_equipment_tab_core(Machine &m) { run_member_tab_core(m, 0); }
void run_equipment_tab(Machine &m) {
    run_equipment_tab_core(m);
    m.ret();
}
// rom: 0x343BA7 (near)
// Transfer tab: select a party member and open the transfer menu.
void run_transfer_tab_core(Machine &m) { run_member_tab_core(m, 1); }
void run_transfer_tab(Machine &m) {
    run_transfer_tab_core(m);
    m.ret();
}
// rom: 0x344AB1 (near)
// Formation tab: select a party member and choose the swap destination.
void run_formation_tab_core(Machine &m) { run_member_tab_core(m, 2); }
void run_formation_tab(Machine &m) {
    run_formation_tab_core(m);
    m.ret();
}

// rom: 0x341FB7 (near)
// Inventory tab: cycle items in either column and open their action menu.
void run_inventory_tab_core(Machine &m) {
    m.ax() = 0;
    m.ds() = 0;
    for (uint16_t off :
         {uint16_t(kTabActionIndex), uint16_t(kTabClassSelectIndex), uint16_t(kTabGenderSelectIndex),
          uint16_t(kTabThirdSelection), uint16_t(kTabListColumn)})
        m.wr8(m.ds(), off, 0);
    tab_clear(m);
    bool redraw = true, description = false;
    for (;;) {
        hooks::emit(hooks::Event::InventoryMenu, m, {1});
        if (redraw) {
            tab_call(m, 0x28E4);
            tab_call(m, 0x2B08);
            tab_upload(m);
            tab_display(m);
        }
        if (redraw || description)
            tab_call(m, 0x8262);
        m.set_al(0);
        m.out8(5, m.al());
        m.set_al(0x18);
        m.out8(6, m.al());
        tab_call(m, 0x28E4);
        tab_upload(m);
        tab_display(m);
        tab_poll(m, 9, kTabClassSelectIndex);
        if (m.rd8(m.ds(), kTabListColumn)) {
            m.dx() = uint16_t((m.dx() & 0xFF00) | (8));
            m.si() = kTabGenderSelectIndex;
        }
        const uint16_t keys = uint16_t(hooks::filter(hooks::Value::InventoryKeys, m, m.ax()));
        redraw = false;
        description = false;
        if (keys & 8) {
            menu_sound(m, 0x94);
            hooks::emit(hooks::Event::InventoryMenu, m, {0});
            return;
        }
        if (keys & 4) {
            menu_sound(m, 0x93);
            const bool crystal = !m.rd8(m.ds(), kTabListColumn) && m.rd8(m.ds(), kTabClassSelectIndex) == 8;
            hooks::emit(hooks::Event::InventoryMenu, m, {crystal ? 0 : 2});
            tab_call(m, 0x20E7);
            if (m.rd8(m.ds(), kTabExitRequested))
                return;
            redraw = true;
        } else if (keys & (0x40 | 0x10)) {
            tab_move(m, (keys & 0x40) != 0);
            menu_sound(m, 0x92);
            description = true;
        } else if (keys & 0x20) {
            if (m.rd8(m.ds(), kTabListColumn) != 1) {
                m.wr8(m.ds(), kTabListColumn, 1);
                menu_sound(m, 0x92);
                description = true;
            }
        } else if (keys & 0x80) {
            if (m.rd8(m.ds(), kTabListColumn) != 0) {
                m.wr8(m.ds(), kTabListColumn, 0);
                menu_sound(m, 0x92);
                description = true;
            }
        }
    }
}
void run_inventory_tab(Machine &m) {
    run_inventory_tab_core(m);
    m.ret();
}

// rom: 0x345FFD (near)
// Save tab: choose a slot, preview its party and confirm the write.
void run_save_tab_core(Machine &m) {
    UiSceneScope ui_scope(m, hooks::Event::SaveMenu, 2);
    m.ax() = 0;
    m.ds() = 0;
    for (uint16_t off : {uint16_t(kTabClassSelectIndex), uint16_t(kTabGenderSelectIndex),
                         uint16_t(kTabExitRequested), uint16_t(kTabActionResult)})
        m.wr8(m.ds(), off, 0);
    tab_call(m, 0x6B04);
    m.wr8(m.ds(), kTabClassSelectIndex, m.al());
    tab_clear(m);
    tab_display(m);
    m.set_al(0);
    m.out8(5, m.al());
    m.set_al(0x20);
    m.out8(6, m.al());
    bool redraw = true;
    for (;;) {
        if (redraw) {
            m.di() = 0x60FC;
            tab_call(m, 0x61C1);
            tab_call(m, 0x61FF);
            tab_call(m, 0x653B);
            tab_upload(m);
        }
        tab_poll(m, 2, kTabClassSelectIndex);
        const uint16_t keys = m.ax();
        redraw = false;
        if (keys & 8) {
            menu_sound(m, 0x94);
            return;
        }
        if (keys)
            hooks::emit(hooks::Event::SaveMenu, m, {2});
        if (keys & 4) {
            const int slot = save_slot_for_row(m, m.rd8(m.ss(), kTabClassSelectIndex));
            if (const SaveSlots *slots = save_store(m); slots && slots->locked(slot)) {
                menu_sound(m, 0x2A);
                hooks::emit(hooks::Event::SaveMenu, m, {7}); // protected file: keep the slot selector
                continue;
            }
            if (slot == SaveSlots::kAuto) {
                menu_sound(m, 0x2A); // AUTO: the autosave is loaded, never saved over
                continue;
            }
            menu_sound(m, 0x93);
            tab_call(m, 0x6102);
            redraw = true;
            continue;
        }
        if (keys & (0x10 | 0x40)) {
            if (SaveSlots *slots = extended_save_slots(m)) { // Save Slots 9: scroll, don't wrap
                if (scroll_save_list(m, *slots, (keys & 0x10) == 0)) {
                    menu_sound(m, 0x92);
                    redraw = true; // the summaries moved
                    continue;
                }
            } else {
                tab_move(m, (keys & 0x10) == 0, true);
            }
            tab_call(m, 0x653B);
            menu_sound(m, 0x92);
        }
        m.di() = 0x60FC;
        tab_call(m, 0x61C1);
        dma_sprite_table_core(m);
        dma_palettes_core(m);
        m.call_far(0x4000, 9);
    }
}
void run_save_tab(Machine &m) {
    run_save_tab_core(m);
    m.ret();
}

// rom: 0x346B29 (near)
// Options tab: select a setting and open its choice screen.
void run_options_tab_core(Machine &m) {
    m.ax() = 0;
    m.ds() = 0;
    for (uint16_t off :
         {uint16_t(kTabActionIndex), uint16_t(kTabClassSelectIndex), uint16_t(kTabGenderSelectIndex),
          uint16_t(kTabListColumn), uint16_t(kTabExitRequested), uint16_t(kTabActionResult)})
        m.wr8(m.ds(), off, 0);
    tab_clear(m);
    tab_display(m);
    m.set_al(0);
    m.out8(5, m.al());
    m.set_al(0xC);
    m.out8(6, m.al());
    bool redraw = true;
    for (;;) {
        if (redraw) {
            m.di() = 0x6C0C;
            tab_call(m, 0x7204);
            tab_call(m, 0x7261);
            tab_call(m, 0x728D);
            tab_upload(m);
        }
        tab_poll(m, 5, kTabClassSelectIndex);
        const uint16_t keys = m.ax();
        redraw = false;
        if (keys & 8) {
            menu_sound(m, 0x94);
            return;
        }
        if (keys & 4) {
            m.bx() = uint16_t(m.rd8(m.ss(), m.si()) * 2);
            m.ax() = m.rd16(m.cs(), uint16_t(0x6BDE + m.bx()));
            tab_call(m, m.ax());
            redraw = true;
            continue;
        }
        if (keys & (0x10 | 0x40)) {
            tab_move(m, (keys & 0x10) == 0, true);
            menu_sound(m, 0x92);
        }
        m.di() = 0x6C0C;
        tab_call(m, 0x7204);
        tab_call(m, 0x7261);
        dma_sprite_table_core(m);
        dma_palettes_core(m);
        m.call_far(0x4000, 9);
    }
}
void run_options_tab(Machine &m) {
    run_options_tab_core(m);
    m.ret();
}

// rom: 0x345139 (near)
// Progress tab: choose the progression/achievement action and cycle the five race categories.
void run_progress_tab_core(Machine &m) {
    UiSceneScope bestiary_scope(m, hooks::Event::Bestiary, 4); // presentation only
    m.ax() = 0;
    m.ds() = 0;
    for (uint16_t off :
         {uint16_t(kTabClassSelectIndex), uint16_t(kTabGenderSelectIndex), uint16_t(kTabThirdSelection),
          uint16_t(kTabFourthSelection), uint16_t(kTabFifthSelection), uint16_t(kTabSixthSelection),
          uint16_t(kTabExitRequested), uint16_t(kTabActionResult), uint16_t(kTabListColumn)})
        m.wr8(m.ss(), off, 0);
    tab_clear(m);
    tab_display(m);
    m.set_al(0);
    m.out8(5, m.al());
    m.set_al(0xC);
    m.out8(6, m.al());
    bool redraw = true;
    for (;;) {
        if (redraw) {
            tab_call(m, 0x54F0);
            tab_call(m, 0x5577);
            hooks::emit(hooks::Event::Bestiary, m, {4}); // catalog restored after a submenu
            tab_upload(m);
        }
        tab_poll(m, 1, kTabClassSelectIndex, true);
        const uint16_t keys = m.ax();
        redraw = false;
        if (keys & 8) {
            menu_sound(m, 0x94);
            return;
        }
        if (keys & 4) {
            menu_sound(m, 0x93);
            m.bx() = uint16_t(m.rd8(m.ss(), m.si()) * 2);
            m.ax() = m.rd16(m.cs(), uint16_t(0x5206 + m.bx()));
            tab_call(m, m.ax());
            if (m.flags() & emu::CF)
                return;
            redraw = true;
            continue;
        }
        if (keys & (0x80 | 0x20)) {
            tab_move(m, (keys & 0x80) == 0, true);
            menu_sound(m, 0x92);
        } else if (keys & (0x800 | 0x200)) {
            m.dx() = uint16_t((m.dx() & 0xFF00) | (4));
            m.si() = kTabListColumn;
            tab_move(m, (keys & 0x800) == 0, true);
            tab_call(m, 0x5CA5);
            menu_sound(m, 0x92);
            const uint16_t ds = m.ds();
            static constexpr TextKey kProgressTabKeys[2] = {TextKey::run_progress_tab_1,
                                                            TextKey::run_progress_tab_2};
            for (unsigned n = 0; n < 2; ++n) {
                const TextPtr t = text(m, kProgressTabKeys[n]);
                m.ds() = t.segment;
                m.ax() = 1;
                m.bx() = uint16_t(0xF03 + n * 0x100);
                m.si() = t.offset;
                draw_text_core(m);
                text_done(m, kProgressTabKeys[n]);
            }
            m.ds() = ds;
        }
        tab_call(m, 0x54F0);
        dma_sprite_table_core(m);
        dma_palettes_core(m);
        m.call_far(0x4000, 9);
    }
}
void run_progress_tab(Machine &m) {
    run_progress_tab_core(m);
    m.ret();
}

} // namespace sawgod::game
