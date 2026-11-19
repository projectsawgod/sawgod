// The game's host screens, owned together: installed on the machine in one fixed order and run after each
// game frame in that order.
#pragma once

#include "emu/wsc.h"
#include "game/battle_ui.h"
#include "game/bestiary_ui.h"
#include "game/creation_ui.h"
#include "game/dialogue_ui.h"
#include "game/inventory_ui.h"
#include "game/item_dialog_ui.h"
#include "game/party_ui.h"
#include "game/save_ui.h"
#include "game/service_ui.h"

namespace sawgod::app {

struct UiScreens {
    game::BattleUi battle;
    game::InventoryUi inventory;
    game::ItemDialogUi item_dialog;
    game::PartyUi party;
    game::CreationUi creation;
    game::BestiaryUi bestiary;
    game::DialogueUi dialogue;
    game::ServiceUi service;
    game::SaveUi save;

    void install(emu::Wsc &w) {
        battle.install(w);
        inventory.install(w);
        item_dialog.install(w, &inventory);
        party.install(w);
        creation.install(w);
        bestiary.install(w);
        dialogue.install(w);
        service.install(w);
        save.install(w);
    }

    // After a game frame. battle_transparency: Options > Battle Transparency, in percent.
    void frame(emu::Wsc &w, int battle_transparency) {
        battle.set_transparency(battle_transparency);
        battle.frame(w);
        inventory.frame(w);
        item_dialog.frame(w);
        party.frame(w);
        creation.frame(w);
        bestiary.frame(w);
        dialogue.frame(w);
        service.frame(w);
        save.frame(w);
    }
};

} // namespace sawgod::app
